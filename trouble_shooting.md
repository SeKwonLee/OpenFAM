## Issue 1: utx and openmpi version-mismatch error while building openmpi-4.0.1

### Error message
```
btl_uct_rdma.h:61:5: error: too few arguments to function 'uct_rkey_unpack'
```

### Solution
```
Reference: https://openucx.readthedocs.io/en/master/running.html#openmpi-with-ucx

NOTE: With OpenMPI 4.0 and above, there could be compilation errors from “btl_uct” component. 
This component is not critical for using UCX; so it could be disabled this way:

Add the following configuration flag to openmpi config.
$ ./configure ... --enable-mca-no-build=btl-uct ...
```

## Issue 2: ruamel.yaml version-mismatch error while running openfam_adm

### Error message
```
===========================================================                                                   
Test OpenFAM with cis-rpc-meta-direct-mem-rpc configuration                                                   
===========================================================                                                   
Traceback (most recent call last):                                                                            
File "/home/leesek/projects/OpenFAM/build//bin/openfam_adm", line 338, in <module>                          
pe_config_doc = ruamel.yaml.load(                                                                         
File "/home/leesek/.local/lib/python3.8/site-packages/ruamel/yaml/main.py", line 1085, in load              
error_deprecation('load', 'load', arg=_error_dep_arg, comment=_error_dep_comment)                         
File "/home/leesek/.local/lib/python3.8/site-packages/ruamel/yaml/main.py", line 1037, in error_deprecation 
raise AttributeError(s)                                                                                   
AttributeError:                                                                                               
"load()" has been removed, use                                                                                

yaml = YAML(typ='rt')                                                                                       
yaml.load(...)                                                                                              

and register any classes that you use, or check the tag attribute on the loaded data,                         
instead of file "/home/leesek/projects/OpenFAM/build//bin/openfam_adm", line 338                              

pe_config_doc = ruamel.yaml.load(                                                                     


OpenFAM test with cis-rpc-meta-direct-mem-rpc configuration failed. exit...
```

### Solution
```
Reference: https://pypi.org/project/ruamel.yaml/

As announced, in 0.18.0, the old PyYAML functions have been deprecated.
(scan, parse, compose, load, emit, serialize, dump and their variants (_all, 
safe_, round_trip_, etc)). If you only read this after your program has stopped 
working: I am sorry to hear that, but that also means you, or the person developing 
your program, has not tested with warnings on (which is the recommendation in PEP 565, 
and e.g. defaultin when using pytest). If you have troubles, explicitly use

pip install "ruamel.yaml<0.18.0"
```

## Issue 3: no --clean option support error for shared memory model while Testing OpenFAM with shared memory configuration (build_and_test.sh)
Need to check if it is intended or unexpected

```
ERROR[1]: Shared memory model does not support --clean option. Please clean FAM and metadata manually
```

## Issue 4: test failures in testing with shared memory config
Error pattern is irregular. Different errors happen in different runs.
Focus on the first failure
Check how to clean up prior environments

### Error
```
The following tests FAILED:                                                                                  
41 - fam_arithmetic_atomics_mt_reg_test (Failed)                                                    
Errors while running CTest                                                                                   
make[3]: *** [test/reg-test/CMakeFiles/reg-test.dir/build.make:57: test/reg-test/CMakeFiles/reg-test] Error 8
make[2]: *** [CMakeFiles/Makefile2:2779: test/reg-test/CMakeFiles/reg-test.dir/all] Error 2                  
make[1]: *** [CMakeFiles/Makefile2:2786: test/reg-test/CMakeFiles/reg-test.dir/rule] Error 2                 
make: *** [Makefile:879: reg-test] Error 2                                                                   
ERROR[1]: Regression test failed                                                                             
OpenFAM test with shared memory configuration failed. exit...

The following tests FAILED:
24 - fam_create_destroy_region_test (Failed)
Errors while running CTest
make[3]: *** [test/unit-test/CMakeFiles/unit-test.dir/build.make:57: test/unit-test/CMakeFiles/unit-test] Error 8
make[2]: *** [CMakeFiles/Makefile2:1448: test/unit-test/CMakeFiles/unit-test.dir/all] Error 2
make[1]: *** [CMakeFiles/Makefile2:1455: test/unit-test/CMakeFiles/unit-test.dir/rule] Error 2
make: *** [Makefile:281: unit-test] Error 2
ERROR[1]: Unit test failed 
OpenFAM test with shared memory configuration failed. exit...

The following tests FAILED:                                                                                  
8 - fam_scatter_gather_index_blocking_reg_test (Failed)                                            
54 - fam_context_reg_test (Failed)                                                                  
Errors while running CTest                                                                                   
make[3]: *** [test/reg-test/CMakeFiles/reg-test.dir/build.make:57: test/reg-test/CMakeFiles/reg-test] Error 8
make[2]: *** [CMakeFiles/Makefile2:2779: test/reg-test/CMakeFiles/reg-test.dir/all] Error 2                  
make[1]: *** [CMakeFiles/Makefile2:2786: test/reg-test/CMakeFiles/reg-test.dir/rule] Error 2                 
make: *** [Makefile:879: reg-test] Error 2                                                                   
ERROR[1]: Regression test failed                                                                             
OpenFAM test with shared memory configuration failed. exit...


```

### Solution

## Issue 5: FAM CIS Client start error
When use `ssh` as a launcher, an error has been observed while sending a start signal.

### Error
```
An exception happens from the following function call.
Source file: src/cis/fam_cis_client.cpp
Function: Fam_CIS_Client(const char *name, uint64_t port)
Code line 55: ::grpc::Status status = stub->signal_start(&ctx, req, &res);
```
### Solution
It has been observed the issue does not happen when disabling ssh.
The following launcher combination also works: using ssh only for a remote memory server

## Issue 6: failures observed by unit-test

### Error
```
98% tests passed, 1 tests failed out of 44

Total Test time (real) =  59.22 sec

The following tests did not run:
         35 - fam_allocate_map_nvmm (Skipped)
         36 - fam_compare_swap_atomics_nvmm_test (Skipped)
         37 - fam_fetch_arithmatic_atomics_nvmm_test (Skipped)
         40 - fam_metadata_test (Skipped)

The following tests FAILED:
         23 - fam_noperm_test (Failed)
```

### Solution



## Issue 7: failures observed by reg-test

### Error
```
94% tests passed, 4 tests failed out of 64

Total Test time (real) = 340.99 sec

The following tests did not run:
64 - fam_map_reg_test (Skipped)

The following tests FAILED:
14 - fam_noperm_reg_test (Failed)
32 - fam_put_get_mt_reg_test (Failed)
33 - fam_scatter_gather_blocking_mt_reg_test (Failed)
36 - fam_put_get_negative_test (Failed)
```

### Solution
