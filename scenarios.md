
# Scenario 1

### Step 1
- Annotation: "System initialized with 3 sites: 2 Control Centers (CC1, CC2) and 1 Data Center (DC). Each site runs 6 replicas."
- Send config:
	- `./config_disseminator -i 1 -c post_configs/scenario1/1_666_standard_1.yaml `
- Switch to pvbrowser progressing

### Step 2
- Annotation: "Control Center 2 has flooded, causing it to fail."
- Kill CC2
    - `./flood_site 2`

### Step 3
- Annotation: "System reconfigured: only CC1 remains active with 6 replicas."
- Switch to config manager
- Send config:
	- `./config_disseminator -i 2 -c post_configs/scenario1/2_6_cc1.yaml`
- Switch to pvbrowser

### Step 4
- Annotation: "Mobile Control Center deployed to restore two-site control."
- Initialize MCC
	- launch config agents on goldenrods
	- launch tails on goldenrods
- Switch to config manager
- Send config:
	- `./config_disseminator -i 3 -c post_configs/scenario1/3_666_mcc.yaml`
- Switch to pvbrowser

### Step 5
- Annotation: "Original Control Center (CC2) restored, Mobile Control Center is no longer needed."
- Restore CC2
- Restart config agents
- Switch to config manager
- Send config:
	- `./config_disseminator -i 4 -c post_configs/scenario1/4_666_standard_2.yaml`
- Switch to pvbrowser

---

# Scenario 2

### Step 1
- Annotation: "System initialized with 3 sites: 2 Control Centers (CC1, CC2) and 1 Data Center (DC). Each site runs 6 replicas."
- Send config:
	- `./config_disseminator -i 1 -c post_configs/scenario2/1_666_standard_1.yaml `
- Switch to pvbrowser progressing

### Step 2
- Annotation: "Control Center 2 has flooded, causing it to fail."
- Kill CC2
    - `./flood_site 2`

### Step 3
- Annotation: "System reconfigured: only CC1 remains active with 6 replicas."
- Switch to config manager
- Send config:
	- `./config_disseminator -i 2 -c post_configs/scenario2/2_6_cc1.yaml`
- Switch to pvbrowser

### Step 3
- Annotation: "Control Center 1 has flooded, causing it to fail. No active control centers remain."
- Kill CC1
- Switch to pvbrowser (should not be progressing)

### Step 4
- Annotation: "Mobile Control Center is deployed as the only control center."
- Initialize MCC
	- launch config agents on goldenrods
	- launch tails on goldenrods
- Switch to config manager
- Send config:
	- `./config_disseminator -i 3 -c post_configs/scenario2/3_6_mcc.yaml`
- Switch to pvbrowser

### Step 5
- Annotation: "Control Center 1 restored, CC1 and the Mobile Control Center are currently active."
- Restore CC1
	- `./restore_site.sh 1`
- Restart config agents
- Switch to config manager
- Send config:
	- `./config_disseminator -i 4 -c post_configs/scenario2/4_666_mcc.yaml`
- Switch to pvbrowser

### Step 6
- Annotation: "Control Center 2 restored,  Mobile Control Center is no longer needed. Original configuration restored (Control Center 1, Control Center 2, Data Center 1)."
- Restore CC1
	- `./restore_site.sh 1`
- Restart config agents
- Switch to config manager
- Send config:
	- `./config_disseminator -i 5 -c post_configs/scenario2/5_666_standard_2.yaml`
- Switch to pvbrowser

---

# Scenario 3

### Step 1
- Annotation: "System initialized with 3 sites: 2 Control Centers (CC1, CC2) and 1 Data Center (DC). Each site runs 6 replicas."
- Switch to config manager
- Send config:
	- `./config_disseminator -i 1 -c post_configs/scenario3/1_666_standard_1.yaml `
- Switch to pvbrowser progressing

### Step 2
- Annotation: "In anticipation of a hurricane, Control Center 2 is replaced with a preemptively deployed Mobile Control Center (MCC)."
- Initialize MCC
	- launch config agents on goldenrods
	- launch tails on goldenrods
- Switch to config manager
- Send config:
	- `./config_disseminator -i 2 -c post_configs/scenario3/2_666_mcc.yaml`
- Switch to pvbrowser 

### Step 3
- Annotation: "After the hurricane passes, both original control centers are operational. System has returned to original configuration (Control Center 1, Control Center 2, Data Center 1)."
- Switch to config manager
- Send config:
	- `./config_disseminator -i 3 -c post_configs/scenario3/3_666_standard_2.yaml`
- Switch to pvbrowser 