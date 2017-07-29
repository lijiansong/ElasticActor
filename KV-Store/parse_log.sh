#!/bin/bash
awk 'BEGIN{print "intf count time"} {a[$2]++; b[$2]+=$3} END{for(x in a) print x, a[x], b[x]}' log
