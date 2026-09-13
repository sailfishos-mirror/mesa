/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_LOWER_H
#define ETHOSU_LOWER_H

#include "ethosu_ml.h"

void
ethosu_lower_graph(struct ethosu_subgraph *subgraph,
                   const struct pipe_ml_operation *poperations, unsigned count);

unsigned
ethosu_feature_map_span(const struct ethosu_feature_map *fm);

#endif /* ETHOSU_LOWER_H */
