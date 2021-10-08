#!/bin/bash
output=$(echo "echo abc" | ./myshell)       # Need to add -n for autograder

# Check myshell errorcode
rc=$?
if [ ${rc} -ne 0 ]
then
    echo "Failed because rc = ${rc}"
    exit $rc
fi

# Failure case: check output == abc
echo $output | grep abc
rc=$?
if [ ${rc} -ne 0 ]
then
    echo "Expected abc, got ${output}"
    exit $rc
fi
