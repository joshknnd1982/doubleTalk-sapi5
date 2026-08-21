// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#define IDD_CONFIG              101
#define IDI_APP                 102

// Parameter controls. Every one is a drop-down list: the values are all
// discrete, and a combo box is the control screen readers handle best - arrow
// keys move through the values and the new value is announced immediately,
// which is exactly the "adjust and hear it at once" interaction this utility
// is for. Sliders announce a bare number with no units and cannot be typed
// into; combo boxes can also be jumped to by typing the value.
#define IDC_VOICE               1001
#define IDC_FILTER              1002
#define IDC_TONE                1003
#define IDC_ARTIC               1004
#define IDC_EXPR                1005
#define IDC_FORMANT             1006
#define IDC_REVERB              1007
#define IDC_RATE                1008
#define IDC_PITCH               1009
#define IDC_VOLUME              1010
#define IDC_BOOST               1011
#define IDC_OUTRATE             1012
#define IDC_LOGLEVEL            1013
#define IDC_PREVIEWTEXT         1014

#define IDC_SPEAK               1020
#define IDC_RESETVOICE          1021
#define IDC_RESETALL            1022
#define IDC_OPENLOGS            1023
#define IDC_STATUS              1024

// Static labels get real control IDs rather than IDC_STATIC. A label sharing
// the -1 id cannot be targeted by WM_GETDLGCODE-style lookups, and giving each
// one an id keeps the label/control pairing unambiguous for accessibility
// tooling that walks the dialog by id.
#define IDC_LBL_VOICE           1101
#define IDC_LBL_FILTER          1102
#define IDC_LBL_TONE            1103
#define IDC_LBL_ARTIC           1104
#define IDC_LBL_EXPR            1105
#define IDC_LBL_FORMANT         1106
#define IDC_LBL_REVERB          1107
#define IDC_LBL_RATE            1108
#define IDC_LBL_PITCH           1109
#define IDC_LBL_VOLUME          1110
#define IDC_LBL_BOOST           1111
#define IDC_LBL_OUTRATE         1112
#define IDC_LBL_LOGLEVEL        1113
#define IDC_LBL_PREVIEWTEXT     1114
