/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_USB_SOURCE_H
#define W202_USB_SOURCE_H

enum w202_usb_bc12_type {
	W202_BC12_UNKNOWN,
	W202_BC12_SDP,
	W202_BC12_DCP,
	W202_BC12_CDP,
};

/* Discard inherited result bits. Only a PHY detection for the current
 * attachment may reintroduce a completed BC1.2 classification.
 */
static inline unsigned int w202_usb_fresh_bc12_status(unsigned int status,
		enum w202_usb_bc12_type detected)
{
	status &= ~0x8e0;
	switch (detected) {
	case W202_BC12_SDP: return status | 0x880;
	case W202_BC12_DCP: return status | 0x840;
	case W202_BC12_CDP: return status | 0x820;
	default: return status;
	}
}

/* W205 UMP9620 sprd-bc1p2: only a present, completed, one-hot result
 * identifies a charger. Old result bits alone do not identify a new cable.
 */
static inline enum w202_usb_bc12_type w202_usb_bc12_type(unsigned int status)
{
	if ((status & 0x804) != 0x804)
		return W202_BC12_UNKNOWN;
	switch (status & 0xe0) {
	case 0x80: return W202_BC12_SDP;
	case 0x40: return W202_BC12_DCP;
	case 0x20: return W202_BC12_CDP;
	default: return W202_BC12_UNKNOWN;
	}
}

/* Read-only UMP9620 register decoding. Units are uA. No PD contract implied.
 * CHARGE_STATUS[2]=VBUS, [11]=BC1.2 done, [7:5]=SDP/DCP/CDP.
 * TYPEC_STATUS[4:0]=state (2 attached sink), [7:6]=live source Rp.
 * TYPEC_EN[0]=enabled, TYPEC_DBG1[8]=VBUS_OK.
 */
static inline unsigned int w202_usb_source_current(unsigned int charge_status,
		unsigned int typec_enable, unsigned int typec_status, unsigned int typec_debug)
{
	unsigned int available = 500000;
	enum w202_usb_bc12_type bc_type = w202_usb_bc12_type(charge_status);

	if (!(charge_status & 0x04))
		return 0;
	if (bc_type == W202_BC12_DCP || bc_type == W202_BC12_CDP)
		available = 1500000;
	if ((typec_enable & 1) && (typec_status & 0x1f) == 2 && (typec_debug & 0x100)) {
		switch ((typec_status >> 6) & 3) {
		case 1:
			return 1500000;
		case 2:
			return 3000000;
		}
	}
	return available;
}

/* TCPM CURRENT_NOW is the negotiated/requested input budget, not measured
 * current. Non-PD must also respect the live Type-C/BC1.2 ceiling.
 */
static inline unsigned int w202_usb_sink_budget(unsigned int advertised,
		unsigned int negotiated, int pd_contract)
{
	if (negotiated > 3000000)
		negotiated = 3000000;
	return pd_contract || negotiated < advertised ? negotiated : advertised;
}

#endif
