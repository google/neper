/*
 * Copyright 2022 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PSP_KERNEL_H_
#define PSP_KERNEL_H_


#define PSP_V0_KEYSIZE 16 /* The size in bytes of a PSP V0 key */

/* PSP (set) tx spi and key */
#define TCP_PSP_TX_SPI_KEY 1730
/* PSP (get) rx spi and key */
#define TCP_PSP_RX_SPI_KEY 1731
/* PSP (get) secure listener */
#define TCP_PSP_LISTENER 1732
/* PSP (get) remote host's SYN spi */
#define TCP_PSP_SYN_SPI 1733
/* Check conn for PSP capability */
#define TCP_PSP_CHECK 1735

#include <linux/types.h>

typedef __u32 psp_generation;
typedef __u32 psp_spi;

struct psp_key {
  __u8 k[PSP_V0_KEYSIZE];
};

struct psp_spi_tuple {
  struct psp_key key;
  psp_generation key_generation;
  psp_spi spi;
};

#endif  // PSP_KERNEL_H_
