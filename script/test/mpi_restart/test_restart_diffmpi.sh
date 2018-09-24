#!/bin/bash

source ../test_common.sh

PROB=${1:-torus}
MADS=${2:-0}

# Must be just a name for now
OUT_DIR=results_$PROB

# Initial clean and make of work area
BASEDIR=../../..
rm -rf build_archive param.dat harm
make -f $BASEDIR/makefile -j4 PROB=$PROB

rm -rf $OUT_DIR
mkdir -p $OUT_DIR

# Give the system a reasonable size to limit runtime
# Bondi problem is 2D
if [ "$PROB" == "bondi" ]; then
  set_problem_size 256 256 1
else
  set_problem_size 96 48 48
fi

# Give a relatively short endpoint
# We're testing init and basic propagation
set_run_dbl tf 1.0
set_run_dbl DTd 1.0
if [ $PROB == "torus" ]
then
  set_run_dbl u_jitter 0.0
fi

for i in $MADS
do

  rm -rf $OUT_DIR/dumps $OUT_DIR/restarts $OUT_DIR/*.h5

  set_cpu_topo 1 1 1

  make -f $BASEDIR/makefile -j4 PROB=$PROB debug

  if [ $PROB == "torus" ]
  then 
    set_run_int mad_type $i
    echo "First run of torus problem, mad_type $i..."
  else
    echo "First run of $PROB problem..."
  fi
  ./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_firsttime.txt
  echo "Done!"

  cd $OUT_DIR
  mv dumps/dump_00000000.h5 ./first_dump_gold.h5
  if [ $PROB == "mhdmodes" ]; then
    mv dumps/dump_00000005.h5 ./last_dump_gold.h5
  else
    mv dumps/dump_00000001.h5 ./last_dump_gold.h5
  fi
  mv restarts/restart_00000001.h5 ./first_restart_gold.h5
  # Leave the grid file and folders
  rm -rf dumps/dump_* restarts/*
  # Copy the restart file back and use it
  cd restarts
  cp ../first_restart_gold.h5 ./restart_00000001.h5
  ln -s restart_00000001.h5 restart.last
  cd ../..

  sleep 1

  if [ $PROB == "bondi" ]
  then
    set_cpu_topo 4 4 1
  else
    set_cpu_topo 2 2 4
  fi
  export OMP_NUM_THREADS=2

  make -f $BASEDIR/makefile -j4 PROB=$PROB debug
  echo "Second run..."
  mpirun -n 16 ./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_secondtime.txt
  echo "Done!"

  ./verify.sh $PROB

  if [ $PROB == "torus" ]
  then
    mv $OUT_DIR/verification_torus.txt $OUT_DIR/verification_torus_$i.txt
  fi

done
