#!/bin/sh
for a in sg??.*; do
  mv $a `echo $a | sed -e s/sg/sg40/g`
done
