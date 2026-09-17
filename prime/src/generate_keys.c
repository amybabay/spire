/*
 * Prime.
 *     
 * The contents of this file are subject to the Prime Open-Source
 * License, Version 1.0 (the ``License''); you may not use
 * this file except in compliance with the License.  You may obtain a
 * copy of the License at:
 *
 * https://jhu-dsn.github.io/prime/LICENSE.txt
 *
 * or in the file ``LICENSE.txt'' found in this distribution.
 *
 * Software distributed under the License is distributed on an AS IS basis, 
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License 
 * for the specific language governing rights and limitations under the 
 * License.
 *
 * Creators:
 *   Yair Amir            yairamir@cs.jhu.edu
 *   Jonathan Kirsch      jak@cs.jhu.edu
 *   John Lane            johnlane@cs.jhu.edu
 *   Marco Platania       platania@cs.jhu.edu
 *   Amy Babay            babay@pitt.edu
 *   Thomas Tantillo      tantillo@cs.jhu.edu 
 *
 *
 * Major Contributors:
 *   Brian Coan           Design of the Prime algorithm
 *   Jeff Seibert         View Change protocol 
 *   Sahiti Bommareddy    Reconfiguration 
 *   Maher Khan           Reconfiguration 
 * 
 *
 *      
 * Copyright (c) 2008-2025
 * The Johns Hopkins University.
 * All rights reserved.
 * 
 * Partial funding for Prime research was provided by the Defense Advanced 
 * Research Projects Agency (DARPA) and the National Science Foundation (NSF).
 * Prime is not necessarily endorsed by DARPA or the NSF.  
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "def.h"
#include "openssl_rsa.h"
#include "tc_wrapper.h"
#include "data_structs.h"
#include "net_types.h"
#include "objects.h"
#include "network.h"
#include "utility.h"
#include "error_wrapper.h"
#include "recon.h"
#include "proactive_recovery.h"

/* Global generation flags */
int gen_rsa = 0;
int gen_tc = 0;

/* Local function definitions */
void Usage(int argc, char **argv);
void Print_Usage(void);

int main(int argc, char **argv) 
{
  Usage(argc, argv);

  printf("Generating key files and writing them to ./keys directory.\n");

  if (gen_rsa) {
    printf("[INFO] Generating RSA public-private keys...\n");
    OPENSSL_RSA_Generate_Keys();
  }

  if (gen_tc) {
    printf("[INFO] Generating Threshold Crypto keys...\n");
    TC_Generate(2*NUM_F + NUM_K + 1, "./keys");
  }

  return 0;
}

void Usage(int argc, char **argv)
{
  while (--argc > 0) {
    argv++;

    if (!strncmp(*argv, "-r", 2)) {
      gen_rsa = 1;
    }
    else if (!strncmp(*argv, "-t", 2)) {
      gen_tc = 1;
    }
    else {
      Print_Usage();
    }
  }

  /* Default to generating both if neither option is specified */
  if (!gen_rsa && !gen_tc) {
    gen_rsa = 1;
    gen_tc = 1;
  }
}

void Print_Usage(void)
{
  printf("Usage: ./gen_keys [-r] [-t]\n"
         "\t[-r : Generate RSA public-private keys only]\n"
         "\t[-t : Generate Threshold Crypto keys only]\n");
  exit(0);
}
