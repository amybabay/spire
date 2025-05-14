### Constant Usage Categories
# Constant Definitions
### Raw constant definitions



# Initialization
### Initialization of global variables using constants from `def.h`.



# QuorumCheck
### Used in protocol logic to determine if enough replicas have responded or agreed 



# KeyGen
### Used as input to cryptographic key generation functions like `TC_Generate`.

- ./prime/src/generate_keys.c:63:  TC_Generate(2*NUM_F + NUM_K + 1, "./keys");

- ./prime/src/generate_keys.c:63:  TC_Generate(2*NUM_F + NUM_K + 1, "./keys");

- ./prime/src/generate_keys.c:63:  TC_Generate(2*NUM_F + NUM_K + 1, "./keys");
- ./common/conf_scada_packets.c:215:    TC_Initialize_Combine_Phase(NUM_SM + 1, TC_MODE_POST_PRIME);
- ./common/conf_scada_packets.c:254:    TC_Destruct_Combine_Phase(NUM_SM + 1, TC_MODE_POST_PRIME);
- ./common/conf_scada_packets.c:312:    TC_Initialize_Combine_Phase(NUM_SM + 1, TC_MODE_PRE_PRIME); //MK TODO: Should this be F?
- ./common/conf_scada_packets.c:369:    TC_Destruct_Combine_Phase(NUM_SM + 1, TC_MODE_PRE_PRIME);









# ConstExpr
### Used in `#define` macros that compute values from constants.


# ArraySize
### Used to define the size of static arrays or fields.


# LoopBound
### Used as an upper bound in loops that iterate over replicas, sites, or shares.


# Validation
### Used in assertions or sanity checks to verify protocol or configuration constraints.


# no longer used
