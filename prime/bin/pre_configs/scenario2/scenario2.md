# Scenario 2:
- Start with 2 control centers + 1 data center
- We lose 1 control center
- Move to singleton control center (on surviving control center)
- We lose the surviving control center
- We introduce the mobile control center and move to it (as singleton)
- We recover one of the original control centers and move to 2 control centers (one mobile) + data center

---

1. System initialized with 3 sites: 2 Control Centers (CC1, CC2) and 1 Data Center (DC). Each site runs 6 replicas.

2. Control Center 2 has failed.

3. System reconfigured: only CC1 remains active with 6 replicas.

4. Control Center 1 has failed. No active control centers remain.

5. Mobile Control Center (MCC) deployed and takes over as singleton control center.

6. Control Center 2 recovered. System reconfigured with MCC and CC2 as control centers, plus the data center.
