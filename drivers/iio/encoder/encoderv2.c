#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>

#define DEVICE_NAME "encoder0"

struct encoder_data {
    struct gpio_desc *irq_gpiod;
    int irq;
    unsigned int count;
    ktime_t last_time;
    s64 delta_ns;

    struct class *enc_class;
    struct device *enc_device;
};

static ssize_t count_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    struct encoder_data *data = dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%d\n", data->count);
}

static ssize_t count_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf, size_t count)
{
    struct encoder_data *data = dev_get_drvdata(dev);
    unsigned int val;
    if (kstrtouint(buf, 10, &val))
        return -EINVAL;
    data->count = val;
    return count;
}

static DEVICE_ATTR_RW(count);

static ssize_t delta_ns_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    struct encoder_data *data = dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%lld\n", data->delta_ns);
}

static DEVICE_ATTR_RO(delta_ns);

static irqreturn_t encoder_irq_handler(int irq, void *dev_id)
{
    struct encoder_data *data = dev_id;
    ktime_t current_time = ktime_get();

    if (ktime_to_ns(data->last_time) != 0) {
        data->delta_ns = ktime_to_ns(ktime_sub(current_time, data->last_time));
    }

    // Increase count value
    data->count++;

    data->last_time = current_time;

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

    // Create sysfs interface
    data->enc_class = class_create(THIS_MODULE, "encoder");
    if (IS_ERR(data->enc_class)) {
        dev_err(&pdev->dev, "Failed to create class\n");
        ret = PTR_ERR(data->enc_class);
        goto err_free;
    }

    data->enc_device = device_create(data->enc_class, NULL, 0, data, DEVICE_NAME);
    if (IS_ERR(data->enc_device)) {
        dev_err(&pdev->dev, "Failed to create device\n");
        ret = PTR_ERR(data->enc_device);
        goto err_class;
    }

    ret = device_create_file(data->enc_device, &dev_attr_count);
    if (ret) {
        dev_err(&pdev->dev, "Failed to create sysfs count file\n");
        goto err_device;
    }

    ret = device_create_file(data->enc_device, &dev_attr_delta_ns);
    if (ret) {
        dev_err(&pdev->dev, "Failed to create sysfs delta_ns file\n");
        goto err_device;
    }

    dev_info(&pdev->dev, "Encoder driver probed successfully\n");
    platform_set_drvdata(pdev, data);
    return 0;

err_device:
    device_destroy(data->enc_class, 0);
err_class:
    class_destroy(data->enc_class);
err_free:
    devm_kfree(&pdev->dev, data);
    return ret;
}

static int encoder_remove(struct platform_device *pdev)
{
    struct encoder_data *data = platform_get_drvdata(pdev);

    device_remove_file(data->enc_device, &dev_attr_delta_ns);
    device_destroy(data->enc_class, 0);
    class_destroy(data->enc_class);
    devm_kfree(&pdev->dev, data);
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
