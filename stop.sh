#!/bin/bash
ps -ax| grep server | grep -v 'grep' | awk -F ' ' ' {print $1} ' | xargs kill -9
