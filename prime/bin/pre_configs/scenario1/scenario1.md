# Scenario 1: 
- Start with 2 control centers + 1 data center
- We lose 1 control center
- Move to singleton control center (on surviving control center)
- We add a mobile control center and move back to 2 control centers (1 is new mobile CC) and data center
- We recover the failed control center and move back to the original configuration 

--- 

1. System initialized with 3 sites: 2 Control Centers (CC1, CC2) and 1 Data Center (DC). Each site runs 6 replicas.

2. Control Center 2 has failed.

3. System reconfigured: only CC1 remains active with 6 replicas

4. Mobile Control Center (MCC) deployed to restore two-site control.

5. Original Control Center (CC2) recovered and reintegrated.