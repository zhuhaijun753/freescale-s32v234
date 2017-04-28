/*
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/input/mt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

/*Deserlizer Registers*/
#define MAX_ID 0x1E
#define MAX_CTRL_CHANNEL 0x0A
#define MAX_CONFIG_CHANNEL0 0x3B
#define MAX_CONFIG_CHANNEL1 0x3F
#define MAX_HIM 0x06

struct max9706 {
    struct i2c_client               *client;
    int powerstate;
    int fsync;
    int video_data_type;
    int pbrs_enable;
};


static struct i2c_client *max_to_i2c(struct max9706 *max)
{
    return max->client;
}


static s32 max_write(const struct i2c_client *client,u8 command, u8 value)
{
    return i2c_smbus_write_byte_data(client, command, value);
}

static s32 max_read(const struct i2c_client *client,u8 reg, u8 *val)
{
    *val=i2c_smbus_write_byte_data(client, reg);
}

unsigned char verify_device(struct max9706 *max)
{
    struct i2c_client *client = max_to_i2c(adv);
    unsigned char id;
    unsigned char retval=1;

    id=max_read(client,MAX_ID, &id);
    if(id != 0x40)
        retval=0;

    return retval;
}

unsigned char disable_high_immunity(struct max9706 *max)
{
    struct i2c_client *client = max_to_i2c(adv);
    unsigned char retval=1;

    retval=max_write(client,MAX_HIM,0x6F);
    if(retval< 0)
    {
        dev_err(&client->dev, "failed to disable dserlizer in him mode\n");
        retval=0;
    }
    return retval;
}

static int init_max(struct max9706 *max) {

    struct i2c_client *client = max_to_i2c(adv);

    if(!verify_device(max)) //Deserlizer not recognized
    {
        dev_err(&client->dev, "failed to detect deserlizer\n");
        return -ENOMEM;
    }
    if(!disable_high_immunity(max))
        return -ENOMEM;
	


}

static int max_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    struct i2c_adapter *adap;
    int err;
    struct max96706 *max;

    adap = to_i2c_adapter(client->dev.parent);

    if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE)) {
        dev_err(&client->dev, "i2c_check_functionality error\n");
        return -ENODEV;
    }

    max = devm_kzalloc(&client->dev, sizeof(*max), GFP_KERNEL);
    if (!max)
        return -ENOMEM;

    max->client = client;
    i2c_set_clientdata(client,max);

    err = init_max(max)
    if(err < 0) {
        dev_err(&client->dev, "Failed to initilize deserlizer");
        return -1;
    }



    static const struct i2c_device_id max_id[] = {
        { "max96706", 0 },
        {               },
    };
    MODULE_DEVICE_TABLE(i2c, max_id);

    static const struct of_device_id max_dt_ids[] = {
        { .compatible = "fsl,max96706", },
        { /* sentinel */                }
    };
    MODULE_DEVICE_TABLE(of, max_dt_ids);

    static struct i2c_driver max_i2c_driver = {
        .driver = {
            .name = "maxim",
            .owner = THIS_MODULE,
            .of_match_table = max_dt_ids,
        },
        .probe = max_probe,
        .remove = max_remove,
        .id_table = max_id,
    };
    module_i2c_driver(max_i2c_driver);

    MODULE_AUTHOR("Nikhil Varghese <nikhilimmu@gmail.com>");
    MODULE_DESCRIPTION("MAX96706/MAX9271 serlizer-deserlizer driver");
    MODULE_LICENSE("GPL");
