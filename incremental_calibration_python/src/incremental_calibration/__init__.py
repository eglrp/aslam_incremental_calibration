# Import the numpy to Eigen type conversion.
import roslib; roslib.load_manifest('numpy_eigen'); import numpy_eigen
roslib.load_manifest('aslam_backend_python'); import aslam_backend

# Import the the C++ exports from your package library.
from libincremental_calibration_python import *
# Import other files in the directory
# from mypyfile import *