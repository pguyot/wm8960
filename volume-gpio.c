#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/workqueue.h>

#define DRV_NAME "volume-gpio"

struct volumegpio_component_data {
	struct snd_soc_component *component;
    struct gpio_descs *gpios;
    struct snd_kcontrol *volume;
	struct work_struct hwvol_work;
	unsigned int in_suspend;
};

static void volumegpio_update_hw_volume(struct work_struct *work)
{
    struct volumegpio_component_data *priv = container_of(work, struct volumegpio_component_data, hwvol_work);
	struct snd_soc_component *component = priv->component;
	struct snd_soc_card *card = component->card;
	struct device *dev = component->dev;
	
	if (dev == NULL)
	    return;

	if (card == NULL)
    	return;

    if (priv->in_suspend)
        return;

    snd_ctl_notify(card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE, &priv->volume->id);
}

static irqreturn_t volumegpio_irq_handler(int irq, void *dev_id)
{
	struct volumegpio_component_data *priv = dev_id;

	schedule_work(&priv->hwvol_work);
	return IRQ_HANDLED;
}

static int volumegpio_volume_kctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
    int ix;
    struct volumegpio_component_data *priv = snd_kcontrol_chip(kcontrol);
    for (ix = 0; ix < priv->gpios->ndescs; ix++) {
        int value = gpiod_get_value(priv->gpios->desc[ix]);
    	ucontrol->value.integer.value[ix] = value;
    }
    
	return 0;
}

static int volumegpio_volume_kctl_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info * uinfo)
{
    struct volumegpio_component_data *priv = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = priv->gpios->ndescs;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static const struct snd_kcontrol_new volumegpio_volume_kctl = {
    .name = "Volume Button",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = volumegpio_volume_kctl_info,
	.get = volumegpio_volume_kctl_get,
};

static const struct snd_kcontrol_new volumegpio_dapm_controls[] = {
    volumegpio_volume_kctl
};

static int volumegpio_component_probe(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	struct volumegpio_component_data *priv;
	int err;
	int irq;
	int ix;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
	priv->component = component;

	priv->gpios = devm_gpiod_get_array(dev, "volume", GPIOD_IN);
	if (IS_ERR(priv->gpios)) {
		err = PTR_ERR(priv->gpios);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'volume' gpios: %d", err);
		return err;
	}

	snd_soc_component_set_drvdata(component, priv);
	
	// Add control.
	priv->volume = snd_ctl_new1(&volumegpio_volume_kctl, priv);
	if (priv->volume == NULL)
        return -ENOMEM;

    priv->volume->id.device = 0;
    priv->volume->id.subdevice = 0;

    err = snd_ctl_add(component->card->snd_card, priv->volume);
	if (err < 0)
	    return err;

	INIT_WORK(&priv->hwvol_work, volumegpio_update_hw_volume);

	// Request interrupts from each GPIO
	for (ix = 0; ix < priv->gpios->ndescs; ix++) {
        irq = gpiod_to_irq(priv->gpios->desc[ix]);
        err = devm_request_any_context_irq(dev, irq,
                        volumegpio_irq_handler, IRQF_TRIGGER_RISING,
                        DRV_NAME, priv);
        if (err < 0)
            return err;
	}

	return 0;
}

static void volumegpio_component_remove(struct snd_soc_component *component)
{
	struct volumegpio_component_data *priv = snd_soc_component_get_drvdata(component);

	cancel_work_sync(&priv->hwvol_work);
}

static int volumegpio_component_suspend(struct snd_soc_component *component)
{
	struct volumegpio_component_data *priv = snd_soc_component_get_drvdata(component);

    priv->in_suspend = 1;
	cancel_work_sync(&priv->hwvol_work);
	
	return 0;
}

static int volumegpio_component_resume(struct snd_soc_component *component)
{
	struct volumegpio_component_data *priv = snd_soc_component_get_drvdata(component);

    priv->in_suspend = 0;
	schedule_work(&priv->hwvol_work);
	
	return 0;
}

static const struct snd_soc_component_driver volumegpio_component_driver = {
    .controls		    = NULL,
	.num_controls		= 0,
	.dapm_widgets		= NULL,
	.num_dapm_widgets	= 0,
	.dapm_routes		= NULL,
	.num_dapm_routes	= 0,
	
	.probe              = volumegpio_component_probe,
	.remove             = volumegpio_component_remove,
	.suspend            = volumegpio_component_suspend,
	.resume             = volumegpio_component_resume,
};


static int volumegpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct snd_soc_component_driver* driver = &volumegpio_component_driver;
	int err;

	err = devm_snd_soc_register_component(dev, driver, NULL, 0);
	if (err < 0)
	    return err;
	
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id volumegpio_ids[] = {
	{ .compatible = "linux,volume-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, volumegpio_ids);
#endif

static struct platform_driver volumegpio_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(volumegpio_ids),
	},
	.probe              = volumegpio_probe
};

module_platform_driver(volumegpio_driver);

MODULE_DESCRIPTION("GPIO-based volume button driver");
MODULE_AUTHOR("Paul Guyot <pguyot@kallisys.net>");
MODULE_LICENSE("GPL");
