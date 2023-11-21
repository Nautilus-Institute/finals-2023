#!/bin/bash

/src/runpoll.sh

RET= $?
if [[ $RET -gt 0 ]]
then
  sleep 1
  echo "trying 2x" >&2
  /src/runpoll.sh
  RET=$?
else
  echo "bye $RET"
  exit $RET
fi


if [ $RET -gt 0 ]
then
  sleep 1
  echo "trying 3x" >&2
  /src/runpoll.sh
  RET=$?
else
  exit $RET
fi

