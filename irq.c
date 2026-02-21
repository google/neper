/*
 * Copyright 2021 Google Inc.
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

/* clang-format off */

#include "irq.h"
#include "common.h"


void get_proc_interrupts(struct stats_irq *irqs)
{
  /* Get the hardware and software interrupt by scraping /proc/stat */
  bool assigned_softirq = false;
  bool assigned_irq = false;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  FILE *f = fopen("/proc/stat", "r");
  if (!f)  {
    fprintf(stderr, "Could not open /proc/stat\n");
    exit(1);
  }

  while ((read = getline(&line, &len, f)) != -1) {
    /* Get NET_TX_SOFTIRQ and NET_RX_SOFTIRQ */
    if (!assigned_softirq)
      assigned_softirq = sscanf(line, "softirq %*u %*u %*u %u %u",
                                &irqs->tx_softirq, &irqs->rx_softirq) == 2;
    /* Record the first number (sum of all intr) from the intr line */
    if (!assigned_irq)
      assigned_irq = sscanf(line, "intr %lu", &irqs->hardirq) == 1;

    if (assigned_softirq && assigned_irq) {
      break;
    }
  }

  if (!assigned_softirq)
    fprintf(stderr,
            "IO or parser error while reading /proc/stat softirq!\n");

  if (!assigned_irq)
    fprintf(stderr,
            "IO or parser error while reading /proc/stat irq!\n");

  if (line)
    free(line);

  fclose(f);

  if (!assigned_softirq || !assigned_irq)
    exit(1);
}

/* clang-format on */
