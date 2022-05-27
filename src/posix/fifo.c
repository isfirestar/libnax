#include "fifo.h"

#include "mxx.h"
#include "io.h"
#include "zmalloc.h"

#define MAXIMUM_FIFO_SIZE       (100)

void fifo_init(ncb_t *ncb)
{
    struct tx_fifo *fifo;

    fifo = &ncb->fifo;
    fifo->tx_overflow = NO;
    fifo->size = 0;
    lwp_mutex_init(&fifo->lock, nsp_true);
    INIT_LIST_HEAD(&fifo->head);
}

void fifo_uninit(ncb_t *ncb)
{
    struct tx_node *node;
    struct tx_fifo *fifo;

    fifo = &ncb->fifo;
    /* cleanup entire FIFO data queue */
    lwp_mutex_lock(&fifo->lock);
    while ((node = list_first_entry_or_null(&fifo->head, struct tx_node, link)) != NULL) {
        list_del(&node->link);
        INIT_LIST_HEAD(&node->link);
        if (node->data) {
            zfree(node->data);
        }
        zfree(node);
    }
    lwp_mutex_unlock(&fifo->lock);
    /* uninitialize the fifo mutex lock */
    lwp_mutex_uninit(&fifo->lock);
}

nsp_status_t fifo_queue(ncb_t *ncb, struct tx_node *node)
{
    nsp_status_t status;
    struct tx_fifo *fifo;

    fifo = &ncb->fifo;
    status = NSP_STATUS_SUCCESSFUL;

    lwp_mutex_lock(&fifo->lock);
    do {
        if ( unlikely(fifo->size >= MAXIMUM_FIFO_SIZE) ) {
            status = posix__makeerror(EBUSY);
            break;
        }
        list_add_tail(&node->link, &fifo->head);

        /* previous Tx request can not complete immediately trigger this function call,
         * so, the IO blocking flag should set, likewise, EPOLLOUT event should assicoated with this @ncb object */
        if (!fifo->tx_overflow) {
            status = io_modify(ncb, EPOLLIN | EPOLLOUT);
            if (!NSP_SUCCESS(status)) {
                list_del(&node->link);
                INIT_LIST_HEAD(&node->link);
                break;
            }
            fifo->tx_overflow = nsp_true;
            mxx_call_ecr("Link:%lld, Tx overflow", ncb->hld);
        }
        ++fifo->size;
    } while(0);

    lwp_mutex_unlock(&fifo->lock);
    return status;
}

nsp_status_t fifo_top(ncb_t *ncb, struct tx_node **node)
{
    struct tx_node *front;
    struct tx_fifo *fifo;

    fifo = &ncb->fifo;
    front = NULL;

    lwp_mutex_lock(&fifo->lock);
    if (NULL != (front = list_first_entry_or_null(&fifo->head, struct tx_node, link))) {
        *node = front;
    }
    lwp_mutex_unlock(&fifo->lock);

    return posix__makeerror( ((NULL == front) ? ENOENT : NSP_STATUS_SUCCESSFUL) );
}

nsp_status_t fifo_pop(ncb_t *ncb, struct tx_node **node)
{
    struct tx_node *front;
    struct tx_fifo *fifo;
    nsp_boolean_t tx_overflow_canceled;

    fifo = &ncb->fifo;
    front = NULL;
    tx_overflow_canceled = nsp_false;

    lwp_mutex_lock(&fifo->lock);
    if (NULL != (front = list_first_entry_or_null(&fifo->head, struct tx_node, link))) {
        /* pop node out from queue */
        list_del(&front->link);
        /* after certain no any other items in the queue but the IO blocking state are still presences,
         * the IO blocking flag should cancel and EPOLLOUT event should disassociation with this @ncb object */
        if (0 == --fifo->size) {
            if (fifo->tx_overflow) {
                fifo->tx_overflow = nsp_false;
                tx_overflow_canceled = nsp_true;
            }
        }
    }
    lwp_mutex_unlock(&fifo->lock);

    if (tx_overflow_canceled) {
        io_modify(ncb, EPOLLIN);
        mxx_call_ecr("Link:%lld, Tx overflow canceled.", ncb->hld);
    }

    if (front) {
        INIT_LIST_HEAD(&front->link);
        if (node) {
            *node = front;
        } else {
            if (front->data) {
                zfree(front->data);
            }
            zfree(front);
        }
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(ENOENT);
}

nsp_boolean_t fifo_tx_overflow(ncb_t *ncb)
{
    struct tx_fifo *fifo;
    nsp_boolean_t tx_overflow;

    fifo = &ncb->fifo;

    lwp_mutex_lock(&fifo->lock);
    tx_overflow = fifo->tx_overflow;
    lwp_mutex_unlock(&fifo->lock);

    return tx_overflow;
}
