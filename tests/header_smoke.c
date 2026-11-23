#include <stddef.h>
#include <stdint.h>

#include <ozero/block.h>
#include <ozero/platform.h>

static bool smoke_irq(o0_gsi_t gsi, void *ctx)
{
    (void)gsi;
    (void)ctx;
    return true;
}

static void smoke_end_io(struct o0_bio *bio, o0_block_status status, size_t done)
{
    (void)bio;
    (void)status;
    (void)done;
}

static o0_status smoke_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio)
{
    (void)queue;
    (void)bio;
    return O0_OK;
}

static o0_status smoke_block_reset(struct o0_block_dev *dev)
{
    (void)dev;
    return O0_OK;
}

static o0_status smoke_dma_constraints(o0_device_t *dev,
                                       struct o0_dma_constraint *out)
{
    (void)dev;
    (void)out;
    return O0_OK;
}

static o0_status smoke_dma_quiesce(o0_device_t *dev)
{
    (void)dev;
    return O0_OK;
}

static o0_status smoke_device_reset(o0_device_t *dev)
{
    (void)dev;
    return O0_OK;
}

static o0_status smoke_io_submit(o0_device_t *dev, struct o0_io_request *req)
{
    (void)dev;
    (void)req;
    return O0_OK;
}

static o0_status smoke_io_cancel(struct o0_io_request *req)
{
    (void)req;
    return O0_OK;
}

int main(void)
{
    o0_irq_handler_t handler = smoke_irq;
    struct o0_block_queue_ops queue_ops = {
        .queue_rq = smoke_queue_rq,
        .poll = NULL,
        .timeout = NULL,
    };
    struct o0_block_ops block_ops = {
        .reset = smoke_block_reset,
    };
    struct o0_device_ops device_ops = {
        .dma_constraints = smoke_dma_constraints,
        .dma_quiesce = smoke_dma_quiesce,
        .reset = smoke_device_reset,
        .io_submit = smoke_io_submit,
        .io_cancel = smoke_io_cancel,
    };
    struct o0_block_queue queue = {
        .id = 0,
        .depth = 32,
        .ops = &queue_ops,
        .driver_data = NULL,
    };
    struct o0_block_dev dev = {
        .name = "smoke0",
        .geo = {
            .logical_block_size = 512,
            .physical_block_size = 4096,
            .nr_sectors = 1024 * 1024,
            .max_hw_sectors = 128,
            .max_segments = 64,
            .max_segment_size = 4096,
            .dma_alignment = 3,
            .queue_depth = 32,
            .features = O0_BLOCK_F_FLUSH | O0_BLOCK_F_ROTATIONAL,
        },
        .nr_queues = 1,
        .queues = &queue,
        .ops = &block_ops,
        .driver_data = NULL,
    };
    struct o0_bio bio = {
        .io = {
            .op = O0_IO_READ,
            .complete = NULL,
        },
        .op = O0_BLOCK_READ,
        .flags = 0,
        .queue_id = 0,
        .tag = 0,
        .nr_vecs = 0,
        .lba = 0,
        .vecs = NULL,
        .end_io = smoke_end_io,
        .private = NULL,
    };

    (void)handler;
    (void)dev;
    (void)device_ops;
    (void)bio;
    (void)o0_block_lba_to_bytes(&dev, 1);
    return 0;
}
