/*
 * Copyright (c) 2014, 2015 EMC Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ntrdma_hello.h"
#include "ntrdma_res.h"
#include "ntrdma_vbell.h"

#define NTRDMA_VER_NONE			0
#define NTRDMA_VER_MIN			1
#define NTRDMA_VER_MAX			1
#define NTRDMA_V1_MAGIC			0x3ce4dbd8
#define NTRDMA_V1_P2_MAGIC		0x1a1530f1
#define NTRDMA_V1_P3_MAGIC		0xe09005ed

struct ntrdma_hello_phase1 {
	/* protocol negotiation */
	u32				version_min;
	u32				version_max;
};

struct ntrdma_hello_phase2 {
	/* protocol validation */
	u32				version_magic;
	u32				phase_magic;

	/* virtual doorbells */
	u64				vbell_addr;
	u32				vbell_count;
	u32				vbell_reserved;

	/* resource commands */
	struct ntrdma_cmd_hello_info	cmd_info;

	/* ethernet frames */
	struct ntrdma_eth_hello_info	eth_info;
};

struct ntrdma_hello_phase3 {
	/* protocol validation */
	u32				version_magic;
	u32				phase_magic;

	/* resource commands */
	struct ntrdma_cmd_hello_prep	cmd_prep;

	/* ethernet frames */
	struct ntrdma_eth_hello_prep	eth_prep;
};

ssize_t ntrdma_dev_hello_phase0(struct ntrdma_dev *dev,
				void *in_buf, size_t in_size,
				void *out_buf, size_t out_size)
{
	if (in_size || out_size)
		return -EINVAL;

	return sizeof(struct ntrdma_hello_phase1);
}

ssize_t ntrdma_dev_hello_phase1(struct ntrdma_dev *dev,
				void *in_buf, size_t in_size,
				void *out_buf, size_t out_size)
{
	struct ntrdma_hello_phase1 *out;

	if (in_size || out_size < sizeof(*out))
		return -EINVAL;

	out = out_buf;

	out->version_min = NTRDMA_VER_MIN;
	out->version_max = NTRDMA_VER_MAX;

	return sizeof(struct ntrdma_hello_phase2);
}

ssize_t ntrdma_dev_hello_phase2(struct ntrdma_dev *dev,
				void *in_buf, size_t in_size,
				void *out_buf, size_t out_size)
{
	struct ntrdma_hello_phase1 *in;
	struct ntrdma_hello_phase2 *out;

	if (in_size < sizeof(*in) || out_size < sizeof(*out))
		return -EINVAL;

	in = in_buf;
	out = out_buf;

	if (in->version_min <= NTRDMA_VER_MAX &&
	    in->version_max >= NTRDMA_VER_MIN) {
		dev->version = min_t(u32, in->version_max, NTRDMA_VER_MAX);
	} else {
		dev->version = NTRDMA_VER_NONE;
		return -EINVAL;
	}

	/* protocol validation */
	out->version_magic = NTRDMA_V1_MAGIC;
	out->phase_magic = NTRDMA_V1_P2_MAGIC;

	/* virtual doorbells */
	out->vbell_addr = dev->vbell_buf_addr;
	out->vbell_count = dev->vbell_count;
	out->vbell_reserved = 0;

	/* command rings */
	ntrdma_dev_cmd_hello_info(dev, &out->cmd_info);

	/* ethernet rings */
	ntrdma_dev_eth_hello_info(dev, &out->eth_info);

	return sizeof(struct ntrdma_hello_phase3);
}

ssize_t ntrdma_dev_hello_phase3(struct ntrdma_dev *dev,
				void *in_buf, size_t in_size,
				void *out_buf, size_t out_size)
{
	struct ntrdma_hello_phase2 *in;
	struct ntrdma_hello_phase3 *out;
	int rc;

	if (in_size < sizeof(*in) || out_size < sizeof(*out))
		return -EINVAL;

	in = in_buf;
	out = out_buf;

	/* protocol validation */
	if (in->version_magic != NTRDMA_V1_MAGIC)
		return -EINVAL;
	if (in->phase_magic != NTRDMA_V1_P2_MAGIC)
		return -EINVAL;

	out->version_magic = NTRDMA_V1_MAGIC;
	out->phase_magic = NTRDMA_V1_P3_MAGIC;

	rc = ntrdma_dev_vbell_enable(dev,
				     ntc_peer_addr(dev->ntc,
						   in->vbell_addr),
				     in->vbell_count);
	if (rc)
		return rc;

	/* command rings */
	rc = ntrdma_dev_cmd_hello_prep(dev, &in->cmd_info, &out->cmd_prep);
	if (rc)
		return rc;

	/* ethernet rings */
	rc = ntrdma_dev_eth_hello_prep(dev, &in->eth_info, &out->eth_prep);
	if (rc)
		return rc;

	return 0;
}

ssize_t ntrdma_dev_hello_phase4(struct ntrdma_dev *dev,
				void *in_buf, size_t in_size,
				void *out_buf, size_t out_size)
{
	struct ntrdma_hello_phase3 *in;

	if (in_size < sizeof(*in) || out_size)
		return -EINVAL;

	in = in_buf;

	/* protocol validation */
	if (in->version_magic != NTRDMA_V1_MAGIC)
		return -EINVAL;
	if (in->phase_magic != NTRDMA_V1_P3_MAGIC)
		return -EINVAL;

	/* command rings */
	ntrdma_dev_cmd_hello_done(dev, &in->cmd_prep);

	/* ethernet rings */
	ntrdma_dev_eth_hello_done(dev, &in->eth_prep);

	return 0;
}

ssize_t ntrdma_dev_hello(struct ntrdma_dev *dev, int phase,
			 void *in_buf, size_t in_size,
			 void *out_buf, size_t out_size)
{
	ntrdma_dbg(dev, "hello phase %d\n", phase);

	switch (phase) {
	case 0:
		return ntrdma_dev_hello_phase0(dev, in_buf, in_size,
					       out_buf, out_size);
	case 1:
		return ntrdma_dev_hello_phase1(dev, in_buf, in_size,
					       out_buf, out_size);
	case 2:
		return ntrdma_dev_hello_phase2(dev, in_buf, in_size,
					       out_buf, out_size);
	case 3:
		return ntrdma_dev_hello_phase3(dev, in_buf, in_size,
					       out_buf, out_size);
	case 4:
		return ntrdma_dev_hello_phase4(dev, in_buf, in_size,
					       out_buf, out_size);
	}
	return -EINVAL;
}
