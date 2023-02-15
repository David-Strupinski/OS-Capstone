# Copyright (c) 2021, 2022 The University of British Columbia.
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.

FROM ubuntu:22.04
MAINTAINER Reto Achermann <achreto@cs.ubc.ca>

COPY install.sh /install.sh
COPY entrypoint.sh /entrypoint.sh

RUN /install.sh && rm -rf /install.sh

ENTRYPOINT ["/entrypoint.sh"]
