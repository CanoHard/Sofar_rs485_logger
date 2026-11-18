#pragma once
// Fault descriptions of the Sofar G3 inverters (registers 0x0405..0x0416).
// Generated from "SOFAR-G3 External Modbus Protocol" V1.09 (same table in V1.10), sheet "Fault Addresses".
// Index = fault ID - 1. Bit n of fault register r is the fault ID (r * 16 + n + 1).

static const char *const FAULT_NAMES[288] = {
    "Grid over-voltage", // ID001
    "Grid undervoltage", // ID002
    "Grid Overfrequency", // ID003
    "Grid Underfrequency", // ID004
    "Leakage current fault", // ID005
    "High penetration error", // ID006
    "Low penetration error", // ID007
    "Islanding error", // ID008
    "Grid voltage transient value overvoltage 1", // ID009
    "Grid voltage transient value overvoltage 2", // ID010
    "Grid line voltage error", // ID011
    "Inverter voltage error", // ID012
    "Anti-backflow overload", // ID013
    "Grid voltage unbalance", // ID014
    "Zero-ground voltage too high", // ID015
    nullptr, // ID016
    "Grid current sampling error", // ID017
    "Grid current DC component sampling error (AC side)", // ID018
    "Grid voltage sampling error (DC side)", // ID019
    "Grid voltage sampling error (AC side)", // ID020
    "Leakage current sampling error (DC side)", // ID021
    "Leakage current sampling error (AC side)", // ID022
    "Load voltage DC component sampling error", // ID023
    "DC input current sampling error", // ID024
    "DC component sampling error of grid current (DC side)", // ID025
    "DC input branch current sampling error", // ID026
    nullptr, // ID027
    "PID abnormal output", // ID028
    "Leakage current consistency error", // ID029
    "Grid voltage consistency error", // ID030
    "DCI consistency error", // ID031
    nullptr, // ID032
    "SPI communication error (DC side)", // ID033
    "SPI communication error (AC side)", // ID034
    "Chip error (DC side)", // ID035
    "Chip error (AC side)", // ID036
    "Auxiliary power error", // ID037
    "Inverter soft start failure", // ID038
    "Arc shutdown protection", // ID039
    "Weak light detection failure", // ID040
    "Relay detection failure", // ID041
    "Low insulation impedance", // ID042
    "Grounding error", // ID043
    "Input mode setting error", // ID044
    "CT error", // ID045
    "Input reversal error", // ID046
    "Parallel error", // ID047
    "Serial number error", // ID048
    "Battery temperature protection", // ID049
    "Heat sink 1 temperature protection", // ID050
    "Heater 2 temperature protection", // ID051
    "Heater 3 temperature protection", // ID052
    "Heatsink 4 temperature protection", // ID053
    "Heatsink 5 temperature protection", // ID054
    "Radiator 6 temperature protection", // ID055
    nullptr, // ID056
    "Ambient temperature 1 protection", // ID057
    "Ambient temperature 2 protection", // ID058
    "Module 1 temperature protection", // ID059
    "Module 2 temperature protection", // ID060
    "Module 3 temperature protection", // ID061
    "Module temperature difference is too large", // ID062
    nullptr, // ID063
    nullptr, // ID064
    "Bus voltage RMS unbalance", // ID065
    "Bus voltage transient value unbalance", // ID066
    "Undervoltage of busbar during grid connection", // ID067
    "Bus bar low voltage", // ID068
    "PV overvoltage", // ID069
    "Battery over-voltage", // ID070
    "LLCBus overvoltage protection", // ID071
    "Inverter bus voltage RMS software overvoltage", // ID072
    "Inverter bus voltage transient value software overvoltage", // ID073
    "Flying Cross Capacitor Overvoltage Protection", // ID074
    "Flying Cross capacitor undervoltage protection", // ID075
    nullptr, // ID076
    nullptr, // ID077
    nullptr, // ID078
    nullptr, // ID079
    nullptr, // ID080
    "Battery overcurrent software protection", // ID081
    "Dci overcurrent protection", // ID082
    "Output transient current protection", // ID083
    "BuckBoost software overcurrent", // ID084
    "Output RMS current protection", // ID085
    "PV instantaneous current overcurrent software protection", // ID086
    "PV parallel uneven current", // ID087
    "Output current unbalance", // ID088
    "PV software overcurrent protection", // ID089
    "Balanced circuit overcurrent protection", // ID090
    "Resonance protection", // ID091
    "Software PWM current limit protection", // ID092
    "PV string software overcurrent", // ID093
    nullptr, // ID094
    nullptr, // ID095
    nullptr, // ID096
    "LLC bus hardware overvoltage", // ID097
    "Inverter bus hardware overvoltage", // ID098
    "BuckBoost hardware overcurrent", // ID099
    "Battery hardware overcurrent", // ID100
    nullptr, // ID101
    "PV hardware overcurrent", // ID102
    "AC output hardware overcurrent", // ID103
    nullptr, // ID104
    "Power meter error", // ID105
    "Serial number model error", // ID106
    "Hardware version mismatch", // ID107
    nullptr, // ID108
    nullptr, // ID109
    "Overload protection 1", // ID110
    "Overload protection 2", // ID111
    "Overload protection 3", // ID112
    "Overtemperature derating", // ID113
    "Frequency down load", // ID114
    "Frequency loading", // ID115
    "Voltage down load", // ID116
    "Voltage loading", // ID117
    nullptr, // ID118
    nullptr, // ID119
    nullptr, // ID120
    "Lightning protection failure (DC)", // ID121
    "Lightning protection failure (AC)", // ID122
    nullptr, // ID123
    "Battery low voltage protection", // ID124
    "Battery low voltage shutdown", // ID125
    "Battery low voltage pre-alarm", // ID126
    nullptr, // ID127
    nullptr, // ID128
    "Output hardware overcurrent permanent fault", // ID129
    "Bus overvoltage permanent fault", // ID130
    "Bus hardware over-voltage permanent fault", // ID131
    "PV uneven flow permanent fault", // ID132
    "Battery overcurrent permanent fault in EPS mode", // ID133
    "Output transient overcurrent permanent fault", // ID134
    "Output current unbalance permanent fault", // ID135
    "Inverter soft start failure fault", // ID136
    "Input mode setting error permanent fault", // ID137
    "Input overcurrent permanent fault", // ID138
    "Input hardware overcurrent permanent fault", // ID139
    "Relay permanent fault", // ID140
    "Bus unbalance permanent fault", // ID141
    "Lightning protection permanent fault - DC side", // ID142
    "Lightning protection permanent fault - AC side", // ID143
    nullptr, // ID144
    "USB fault", // ID145
    "WIFI fault", // ID146
    "Bluetooth fault", // ID147
    "RTC clock fault", // ID148
    "Communication board EEPROM error", // ID149
    "Communication board FLASH error", // ID150
    nullptr, // ID151
    "Safety regulation version error", // ID152
    "SCI communication error (DC side)", // ID153
    "SCI communication error (AC side)", // ID154
    "SCI communication error (convergence board side)", // ID155
    "Software version inconsistency", // ID156
    "Lithium battery 1 communication error", // ID157
    "Li-ion battery 2 communication error", // ID158
    "Lithium battery 3 communication error", // ID159
    "Lithium battery 4 communication failure", // ID160
    "Forced shutdown", // ID161
    "Remote shutdown", // ID162
    "Drms0 shutdown", // ID163
    nullptr, // ID164
    "Remote down load", // ID165
    "Logic interface down load", // ID166
    "Anti-Reverse Flow Downgrade", // ID167
    nullptr, // ID168
    "Fan 1 failure", // ID169
    "Fan 2 failure", // ID170
    "Fan 3 failure", // ID171
    "Fan 4 failure", // ID172
    "Fan 5 failure", // ID173
    "Fan 6 failure", // ID174
    "Fan 7 fault", // ID175
    "Meter communication failure", // ID176
    "BMS over-voltage alarm", // ID177
    "BMS undervoltage alarm", // ID178
    "BMS high temperature alarm", // ID179
    "BMS low temperature alarm", // ID180
    "BMS charge/discharge overload alarm", // ID181
    "BMS short circuit alarm", // ID182
    "BMS version inconsistency", // ID183
    "BMS CAN version inconsistency", // ID184
    "BMS CAN version is too low", // ID185
    nullptr, // ID186
    nullptr, // ID187
    nullptr, // ID188
    "Arc device communication failure", // ID189
    "DC arc alarm fault", // ID190
    "PID repair failed", // ID191
    "PLC module heartbeat loss", // ID192
    "String fuse open circuit alarm 1-1", // ID193
    "String fuse open circuit alarm 1-2", // ID194
    "String fuse open circuit alarm 2-1", // ID195
    "String fuse open circuit alarm 2-2", // ID196
    "String fuse open circuit alarm 3-1", // ID197
    "String fuse open circuit alarm 3-2", // ID198
    "String fuse open circuit alarm 4-1", // ID199
    "String fuse open circuit alarm 4-2", // ID200
    "String fuse open circuit alarm 5-1", // ID201
    "String fuse open circuit alarm 5-2", // ID202
    "String fuse open circuit alarm 6-1", // ID203
    "String fuse open circuit alarm 6-2", // ID204
    "String fuse open circuit alarm 7-1", // ID205
    "String fuse open circuit alarm 7-2", // ID206
    "String fuse open circuit alarm 8-1", // ID207
    "String fuse open circuit alarm 8-2", // ID208
    "String fuse open circuit alarm 9-1", // ID209
    "String fuse open circuit alarm 9-2", // ID210
    "String fuse open circuit alarm 10-1", // ID211
    "String fuse open circuit alarm 10-2", // ID212
    "String fuse open circuit alarm 11-1", // ID213
    "String fuse open circuit alarm 11-2", // ID214
    "String fuse open circuit alarm 12-1", // ID215
    "String fuse open circuit alarm 12-2", // ID216
    "String fuse open circuit alarm 13-1", // ID217
    "String fuse open circuit alarm 13-2", // ID218
    "String fuse open circuit alarm 14-1", // ID219
    "String fuse open circuit alarm 14-2", // ID220
    "String fuse open circuit alarm 15-1", // ID221
    "String fuse open circuit alarm 15-2", // ID222
    "String fuse open circuit alarm 16-1", // ID223
    "String fuse open circuit alarm 16-2", // ID224
    "Battery input fuse open circuit alarm 0", // ID225
    "Battery input fuse open circuit alarm 1", // ID226
    "Battery input fuse open circuit alarm 2", // ID227
    "Battery input fuse open circuit alarm 3", // ID228
    "Battery input fuse open circuit alarm 4", // ID229
    "Battery input fuse open circuit alarm 5", // ID230
    "Battery input fuse open circuit alarm 6", // ID231
    "Battery input fuse open circuit alarm 7", // ID232
    "Battery input fuse open circuit alarm 8", // ID233
    "Battery input fuse open circuit alarm 9", // ID234
    "Battery input fuse open circuit alarm 10", // ID235
    "Battery input fuse open circuit alarm 11", // ID236
    "Battery input fuse open circuit alarm 12", // ID237
    "Battery input fuse open circuit alarm 13", // ID238
    "Battery input fuse open circuit alarm 14", // ID239
    "Battery input fuse open circuit alarm 15", // ID240
    "CombinerOVGp1", // ID241
    "CombinerOVGp2", // ID242
    "CombinerOVGp3", // ID243
    "CombinerOVGp4", // ID244
    "CombinerOVGp5", // ID245
    "CombinerOVGp6", // ID246
    "CombinerOVGp7", // ID247
    "CombinerOVGp8", // ID248
    "CombinerOVGp9", // ID249
    "CombinerOVGp10", // ID250
    "CombinerOVGp11", // ID251
    "CombinerOVGp12", // ID252
    "CombinerOVGp13", // ID253
    "CombinerOVGp14", // ID254
    "CombinerOVGp15", // ID255
    "CombinerOVGp16", // ID256
    "CombinerUVGp1", // ID257
    "CombinerUVGp2", // ID258
    "CombinerUVGp3", // ID259
    "CombinerUVGp4", // ID260
    "CombinerUVGp5", // ID261
    "CombinerUVGp6", // ID262
    "CombinerUVGp7", // ID263
    "CombinerUVGp8", // ID264
    "CombinerUVGp9", // ID265
    "CombinerUVGp10", // ID266
    "CombinerUVGp11", // ID267
    "CombinerUVGp12", // ID268
    "CombinerUVGp13", // ID269
    "CombinerUVGp14", // ID270
    "CombinerUVGp15", // ID271
    "CombinerUVGp16", // ID272
    "CombinerOC1", // ID273
    "CombinerOC2", // ID274
    "CombinerOC3", // ID275
    "CombinerOC4", // ID276
    "CombinerOC5", // ID277
    "CombinerOC6", // ID278
    "CombinerOC7", // ID279
    "CombinerOC8", // ID280
    "CombinerOC9", // ID281
    "CombinerOC10", // ID282
    "CombinerOC11", // ID283
    "CombinerOC12", // ID284
    "CombinerOC13", // ID285
    "CombinerOC14", // ID286
    "CombinerOC15", // ID287
    "CombinerOC16", // ID288
};
