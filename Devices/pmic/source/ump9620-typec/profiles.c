// SPDX-License-Identifier: GPL-2.0
/* TCPM's standard selectable PD profiles. Both source profiles stay 5 V/500 mA. */
#include <linux/slab.h>
#include "controller.h"

void w202_typec_connector_remove(struct w202_typec *pd)
{
	int i;

	for (i = 2; i >= 0; i--)
		software_node_unregister(&pd->profile_nodes[i]);
	for (i = 0; i < 2; i++) {
		property_entries_free(pd->profile_properties[i]);
		pd->profile_properties[i] = NULL;
	}
	fwnode_remove_software_node(pd->tcpc.fwnode);
	pd->tcpc.fwnode = NULL;
}

int w202_typec_connector_create(struct w202_typec *pd, unsigned int target_mv,
			       unsigned int pps_max_mv, bool pps)
{
	u32 flags = PDO_FIXED_USB_COMM | PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP;
	u32 sources[] = { PDO_FIXED(5000, 500, flags) };
	u32 sinks[] = {
		PDO_FIXED(5000, 3000, flags),
		PDO_FIXED(target_mv, min(3000U, 18000000 / target_mv), 0),
		PDO_PPS_APDO(5000, pps_max_mv, 3000),
	};
	struct property_entry fast[] = {
		PROPERTY_ENTRY_U32_ARRAY("source-pdos", sources),
		PROPERTY_ENTRY_U32_ARRAY_LEN("sink-pdos", sinks,
			pps ? 3 : target_mv == 5000 ? 1 : 2),
		PROPERTY_ENTRY_U32("op-sink-microwatt", 7500000), { }
	};
	struct property_entry normal[] = {
		PROPERTY_ENTRY_U32_ARRAY("source-pdos", sources),
		PROPERTY_ENTRY_U32_ARRAY_LEN("sink-pdos", sinks, 1),
		PROPERTY_ENTRY_U32("op-sink-microwatt", 7500000), { }
	};
	struct property_entry root[] = {
		PROPERTY_ENTRY_STRING("power-role", "dual"),
		PROPERTY_ENTRY_STRING("try-power-role", "sink"),
		PROPERTY_ENTRY_STRING("data-role", "dual"),
		PROPERTY_ENTRY_BOOL("self-powered"), { }
	};
	const struct software_node *group[] = {
		&pd->profile_nodes[0], &pd->profile_nodes[1],
		&pd->profile_nodes[2], NULL,
	};
	int ret;

	pd->tcpc.fwnode = fwnode_create_software_node(root, NULL);
	if (IS_ERR(pd->tcpc.fwnode))
		return PTR_ERR(pd->tcpc.fwnode);
	pd->profile_properties[0] = property_entries_dup(fast);
	pd->profile_properties[1] = property_entries_dup(normal);
	if (IS_ERR(pd->profile_properties[0]) || IS_ERR(pd->profile_properties[1])) {
		ret = IS_ERR(pd->profile_properties[0]) ? PTR_ERR(pd->profile_properties[0]) :
			PTR_ERR(pd->profile_properties[1]);
		if (IS_ERR(pd->profile_properties[0]))
			pd->profile_properties[0] = NULL;
		if (IS_ERR(pd->profile_properties[1]))
			pd->profile_properties[1] = NULL;
		goto error;
	}
	pd->profile_nodes[0] = (struct software_node) {
		.name = "capabilities", .parent = to_software_node(pd->tcpc.fwnode),
	};
	pd->profile_nodes[1] = (struct software_node) {
		.name = "fast", .parent = &pd->profile_nodes[0],
		.properties = pd->profile_properties[0],
	};
	pd->profile_nodes[2] = (struct software_node) {
		.name = "normal", .parent = &pd->profile_nodes[0],
		.properties = pd->profile_properties[1],
	};
	ret = software_node_register_node_group(group);
	if (!ret)
		return 0;
error:
	w202_typec_connector_remove(pd);
	return ret;
}
