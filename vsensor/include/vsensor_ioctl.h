#ifndef VSENSOR_IOCTL_H
#define VSENSOR_IOCTL_H

#include <linux/ioctl.h>

#define VSENSOR_MAGIC 'V'

#define VSENSOR_SET_INTERVAL \
    _IOW(VSENSOR_MAGIC, 1, unsigned int)

#define VSENSOR_GET_INTERVAL \
    _IOR(VSENSOR_MAGIC, 2, unsigned int)

#endif
