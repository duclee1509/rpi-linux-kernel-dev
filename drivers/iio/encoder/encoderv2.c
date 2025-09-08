#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#define ENCODER_PPR 20

struct encoder_data {
    struct gpio_desc *irq_gpiod;
    int irq;
    unsigned int count;
};

static irqreturn_t encoder_irq_handler(int irq, void *dev_id)
{
    struct encoder_data *data = dev_id;

    data->count++;

    pr_info("Encoder count: %u\n", data->count);

    return IRQ_HANDLED;
}

static int encoder_probe(struct platform_device *pdev)
{
    struct encoder_data *data;
    int ret;

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    // Initialize encoder counter
    data->count = 0;

    data->irq_gpiod = devm_gpiod_get(&pdev->dev, "irq", GPIOD_IN);
    if (IS_ERR(data->irq_gpiod)) {
        dev_err(&pdev->dev, "Failed to get IRQ GPIO\n");
        return PTR_ERR(data->irq_gpiod);
    }

    data->irq = gpiod_to_irq(data->irq_gpiod);
    if (data->irq < 0) {
        dev_err(&pdev->dev, "Failed to get IRQ number\n");
        return data->irq;
    }

    ret = devm_request_threaded_irq(&pdev->dev, data->irq, NULL,
                                    encoder_irq_handler,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                    dev_name(&pdev->dev), data);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ\n");
        return ret;
    }

    dev_info(&pdev->dev, "Encoder driver probed successfully\n");
    platform_set_drvdata(pdev, data);
    return 0;
}

static int encoder_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Encoder driver removed\n");
    return 0;
}

static const struct of_device_id encoder_of_match[] = {
    { .compatible = "encoder-module-v2" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, encoder_of_match);

static struct platform_driver encoder_driver = {
    .probe = encoder_probe,
    .remove = encoder_remove,
    .driver = {
        .name = "encoderv2",
        .of_match_table = encoder_of_match,
    },
};

module_platform_driver(encoder_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Duc Lee");
MODULE_DESCRIPTION("Encoder Driver");
