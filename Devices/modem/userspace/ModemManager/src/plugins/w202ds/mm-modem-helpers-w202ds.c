/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _LIBMM_INSIDE_MM
#include "mm-modem-helpers-w202ds.h"

MMSignal *
mm_w202ds_cesq_nr_signal (const gchar *response)
{
    g_auto(GStrv) fields = NULL;
    guint64 level[9];
    guint i;
    MMSignal *signal;

    if (!response)
        return NULL;
    while (g_ascii_isspace (*response))
        response++;
    if (!g_str_has_prefix (response, "+CESQ:"))
        return NULL;

    fields = g_strsplit (response + sizeof ("+CESQ:") - 1, ",", -1);
    if (g_strv_length (fields) != G_N_ELEMENTS (level))
        return NULL;
    for (i = 0; i < G_N_ELEMENTS (level); i++) {
        if (!g_ascii_string_to_unsigned (g_strstrip (fields[i]), 10, 0, 255, &level[i], NULL))
            return NULL;
    }

    /* TS 27.007 §8.69: fields 7–9 are SS-RSRQ, SS-RSRP and SS-SINR.
     * 255 is unknown. Use the lower bound of each bin and clamp bin 0
     * to the bottom of the measurable range. */
    if (level[6] > 126 && level[7] > 126 && level[8] > 127)
        return NULL;
    signal = mm_signal_new ();
    if (level[6] <= 126)
        mm_signal_set_rsrq (signal, -43.0 + 0.5 * (MAX (level[6], 1) - 1));
    if (level[7] <= 126)
        mm_signal_set_rsrp (signal, -156.0 + (MAX (level[7], 1) - 1));
    if (level[8] <= 127)
        mm_signal_set_snr (signal, -23.0 + 0.5 * (MAX (level[8], 1) - 1));
    return signal;
}

GRegex *
mm_w202ds_sim_urc_regex_new (void)
{
    /* The stock RIL routes ECIND group 3 to onSimStatusChanged(). Only use
     * it as a hint: confirm presence with CPIN instead of guessing the
     * meaning of every proprietary status/reason value. Do not consume
     * +CPIN replies here; the generic unlock query still needs them. */
    return g_regex_new ("\\r\\n\\+ECIND:[ \\t]*3[ \\t]*,[^\\r\\n]+\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

MMW202dsSimAction
mm_w202ds_sim_check_action (gboolean                  has_sim,
                            MMModemState              state,
                            MMModemStateFailedReason  failed_reason,
                            MMModemLock               lock,
                            const GError             *error)
{
    if (error) {
        if (has_sim && g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR,
                                        MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED))
            return MM_W202DS_SIM_ACTION_REMOVED;
        return MM_W202DS_SIM_ACTION_NONE;
    }

    if (lock == MM_MODEM_LOCK_UNKNOWN)
        return MM_W202DS_SIM_ACTION_NONE;

    if (!has_sim && state == MM_MODEM_STATE_FAILED &&
        failed_reason == MM_MODEM_STATE_FAILED_REASON_SIM_MISSING)
        return MM_W202DS_SIM_ACTION_INSERTED;

    if (has_sim && lock == MM_MODEM_LOCK_NONE)
        return MM_W202DS_SIM_ACTION_CHECK_IDENTITY;

    return MM_W202DS_SIM_ACTION_NONE;
}
