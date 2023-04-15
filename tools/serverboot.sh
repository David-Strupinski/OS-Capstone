#!/bin/bash

UTSERVER=roland.cs.washington.edu

scp $1 $UTSERVER:~
ssh -t $UTSERVER "pandaboot `basename $1`"
