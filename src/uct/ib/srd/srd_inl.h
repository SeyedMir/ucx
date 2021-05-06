/**
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * See file LICENSE for terms.
 */

#include "srd_def.h"

#include <uct/ib/base/ib_log.h>


static UCS_F_ALWAYS_INLINE void
uct_srd_ep_ctl_op_schedule(uct_srd_iface_t *iface, uct_srd_ep_t *ep)
{
    ucs_arbiter_group_push_elem(&ep->tx.pending.group,
                                &ep->tx.pending.elem);
    ucs_arbiter_group_schedule(&iface->tx.pending_q, &ep->tx.pending.group);
}

/**
 * schedule control operation.
 */
static UCS_F_ALWAYS_INLINE void
uct_srd_ep_ctl_op_add(uct_srd_iface_t *iface, uct_srd_ep_t *ep, int op)
{
    ep->tx.pending.ops |= op;
    uct_srd_ep_ctl_op_schedule(iface, ep);
}

/*
 * check iface resources:tx_queue and return
 * prefetched/cached skb
 *
 * NOTE: caller must not return skb to mpool until it is
 * removed from the cache
 * skb is removed from cache by uct_srd_iface_complete_tx()
 *
 * In case of error flow caller must do nothing with the skb
 */
static UCS_F_ALWAYS_INLINE
uct_srd_send_skb_t *uct_srd_iface_get_tx_skb(uct_srd_iface_t *iface,
                                             uct_srd_ep_t *ep)
{
    uct_srd_send_skb_t *skb;

    if (ucs_unlikely(!uct_srd_iface_can_tx(iface))) {
        UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
        return NULL;
    }

    skb = iface->tx.skb;
    if (ucs_unlikely(skb == NULL)) {
        skb = ucs_mpool_get(&iface->tx.mp);
        if (skb == NULL) {
            ucs_trace_data("iface=%p out of tx skbs", iface);
            UCT_TL_IFACE_STAT_TX_NO_DESC(&iface->super.super);
            return NULL;
        }
        iface->tx.skb = skb;
    }
    VALGRIND_MAKE_MEM_DEFINED(&skb->lkey, sizeof(skb->lkey));
    skb->flags = 0;
    ucs_prefetch(skb->neth);
    return skb;
}

/* same as above but also check ep resources: connection state */
static UCS_F_ALWAYS_INLINE uct_srd_send_skb_t *
uct_srd_ep_get_tx_skb(uct_srd_iface_t *iface, uct_srd_ep_t *ep)
{
    if (ucs_unlikely(!uct_srd_ep_is_connected_and_no_pending(ep))) {
        ucs_trace_poll("iface=%p ep=%p (%d->%d) no ep resources (psn=%u)",
                       iface, ep, ep->ep_id, ep->dest_ep_id,
                       (unsigned)ep->tx.psn);
        UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
        return NULL;
    }

    return uct_srd_iface_get_tx_skb(iface, ep);
}

static UCS_F_ALWAYS_INLINE void
uct_srd_skb_release(uct_srd_send_skb_t *skb, int is_inline)
{
    ucs_assert(!(skb->flags & UCT_SRD_SEND_SKB_FLAG_INVALID));
    skb->flags = UCT_SRD_SEND_SKB_FLAG_INVALID;
    if (is_inline) {
        ucs_mpool_put_inline(skb);
    } else {
        ucs_mpool_put(skb);
    }
}

#if UCS_ENABLE_ASSERT
static UCS_F_ALWAYS_INLINE int uct_srd_ep_has_pending(uct_srd_ep_t *ep)
{
    return !ucs_arbiter_group_is_empty(&ep->tx.pending.group) &&
           !ucs_arbiter_elem_is_only(&ep->tx.pending.elem);
}
#endif

static UCS_F_ALWAYS_INLINE void uct_srd_ep_set_has_pending_flag(uct_srd_ep_t *ep)
{
    ep->flags |= UCT_SRD_EP_FLAG_HAS_PENDING;
}

static UCS_F_ALWAYS_INLINE void uct_srd_ep_remove_has_pending_flag(uct_srd_ep_t *ep)
{
    ucs_assert(ep->flags & UCT_SRD_EP_FLAG_HAS_PENDING);
    ep->flags &= ~UCT_SRD_EP_FLAG_HAS_PENDING;
}

static UCS_F_ALWAYS_INLINE void uct_srd_ep_set_dest_ep_id(uct_srd_ep_t *ep,
                                                          uint32_t dest_id)
{
    ucs_assert(dest_id != UCT_SRD_EP_NULL_ID);
    ep->dest_ep_id = dest_id;
    ep->flags     |= UCT_SRD_EP_FLAG_CONNECTED;
}

static UCS_F_ALWAYS_INLINE void
uct_srd_skb_set_comp_desc(uct_srd_send_skb_t *skb, uct_completion_t *comp)
{
    if (comp != NULL) {
        skb->flags |= UCT_SRD_SEND_SKB_FLAG_COMP;
        uct_srd_comp_desc(skb)->comp = comp;
    }
}

static UCS_F_ALWAYS_INLINE void
uct_srd_post_send(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                  struct ibv_send_wr *wr, unsigned send_flags,
                  unsigned max_log_sge)
{
    struct ibv_send_wr *bad_wr;
    int ret;

    wr->wr.ud.remote_qpn = ep->peer_address.dest_qpn;
    wr->wr.ud.ah         = ep->peer_address.ah;
    wr->send_flags       = send_flags;

    ret = ibv_post_send(iface->qp, wr, &bad_wr);
    if (ret != 0) {
        ucs_fatal("ibv_post_send() returned %d (%m)", ret);
    }

    uct_ib_log_post_send(&iface->super, iface->qp, wr, max_log_sge,
                         uct_srd_dump_packet);
}

static UCS_F_ALWAYS_INLINE void
uct_srd_ep_tx_inlv(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                   uct_srd_send_skb_t *skb, const void *buffer, unsigned length)
{
    iface->tx.sge[1].addr    = (uintptr_t)buffer;
    iface->tx.sge[1].length  = length;
    iface->tx.wr_inl.num_sge = 2;
    iface->tx.wr_inl.wr_id   = (uintptr_t)skb;
    skb->neth->psn           = ep->tx.psn++;
    uct_srd_post_send(iface, ep, &iface->tx.wr_inl, IBV_SEND_INLINE, 2);
}

static UCS_F_ALWAYS_INLINE void
uct_srd_ep_tx_skb(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                  uct_srd_send_skb_t *skb, unsigned send_flags,
                  unsigned max_log_sge)
{
    iface->tx.sge[0].lkey   = skb->lkey;
    iface->tx.sge[0].length = skb->len;
    iface->tx.sge[0].addr   = (uintptr_t)skb->neth;
    iface->tx.wr_skb.wr_id  = (uintptr_t)skb;
    skb->neth->psn          = ep->tx.psn++;
    uct_srd_post_send(iface, ep, &iface->tx.wr_skb, send_flags, max_log_sge);
}

static UCS_F_ALWAYS_INLINE void
uct_srd_iface_complete_tx(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                          uct_srd_send_skb_t *skb)
{
    iface->tx.skb = ucs_mpool_get(&iface->tx.mp);
    iface->tx.available--;
    skb->ep = ep;
    ucs_queue_push(&ep->tx.outstanding_q, &skb->out_queue);
}

static UCS_F_ALWAYS_INLINE void
uct_srd_neth_set_type_am(uct_srd_ep_t *ep, uct_srd_neth_t *neth, uint8_t id)
{
    neth->packet_type = (id << UCT_SRD_PACKET_AM_ID_SHIFT) |
                        ep->dest_ep_id |
                        UCT_SRD_PACKET_FLAG_AM;
}

static UCS_F_ALWAYS_INLINE void
uct_srd_neth_init_data(uct_srd_ep_t *ep, uct_srd_neth_t *neth)
{
    neth->psn = ep->tx.psn;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_srd_skb_common(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                   uct_srd_send_skb_t **skb_p)
{
    uct_srd_send_skb_t *skb = uct_srd_ep_get_tx_skb(iface, ep);
    if (!skb) {
        return UCS_ERR_NO_RESOURCE;
    }

    /* either we are executing pending operations,
     * or there are no any pending elements.
     */
    ucs_assertv((ep->flags & UCT_SRD_EP_FLAG_IN_PENDING) ||
                !uct_srd_ep_has_pending(ep),
                "out-of-order send detected for ep %p ep_pending %d arbelem %p",
                ep, (ep->flags & UCT_SRD_EP_FLAG_IN_PENDING),
                &ep->tx.pending.elem);

    uct_srd_neth_init_data(ep, skb->neth);
    *skb_p = skb;
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_srd_am_skb_common(uct_srd_iface_t *iface, uct_srd_ep_t *ep, uint8_t id,
                      uct_srd_send_skb_t **skb_p)
{
    ucs_status_t status;

    UCT_CHECK_AM_ID(id);

    status = uct_srd_skb_common(iface, ep, skb_p);
    if (status != UCS_OK) {
        return status;
    }

    uct_srd_neth_set_type_am(ep, (*skb_p)->neth, id);

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_srd_rdma_skb_common(uct_srd_iface_t *iface, uct_srd_ep_t *ep,
                        uct_srd_send_skb_t **skb_p)
{
    ucs_status_t status = uct_srd_skb_common(iface, ep, skb_p);
    if (status != UCS_OK) {
        return status;
    }

    (*skb_p)->flags |= UCT_SRD_SEND_SKB_FLAG_RDMA;

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE size_t
uct_srd_skb_bcopy(uct_srd_send_skb_t *skb, uct_pack_callback_t pack_cb, void *arg)
{
    size_t payload_len;

    payload_len = pack_cb(skb->neth + 1, arg);
    skb->len = sizeof(skb->neth[0]) + payload_len;
    return payload_len;
}
