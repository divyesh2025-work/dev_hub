# dev_hub
[ { "isProAccount": true, "legType": 0, "lotSize": 2350, "optionTypeCode": 0, "stream": 15, "strikePrice": 0, "symbolToken": 51947 }, { "isProAccount": true, "legType": 1, "lotSize": 2350, "optionTypeCode": 1, "stream": 15, "strikePrice": 25500, "symbolToken": 121349 }, { "isProAccount": true, "legType": 2, "lotSize": 2350, "optionTypeCode": 0, "stream": 15, "strikePrice": 25500, "symbolToken": 121350 } ]
59315

92694
92696


51947
121349
121350
25500


2350


    {59315, 1023, 18, 1443709800, "FEDERALBNK", 28180},

  {92650, 0, 18, 1443709800, "FEDERAKBANK", 600}, // Index future
    {92658, 0, 18, 1443709800, "FEDERAKBANK", 1100} // Index future







trading_platform/
├── strategy_sdk.h                 # Your existing C header
├── strategies/
│   └── conrev_ioc_cpp/
│       └── conrev_ioc.cpp        # Your existing C++ strategy
├── python_sdk/
│   ├── setup.py                  # Cython build configuration
│   ├── strategy_types.pxd        # Cython declarations (C types)
│   ├── platform_api.pyx          # Python wrapper for PlatformAPI
│   └── base_strategy.pyx         # Base class for Python strategies
└── python_strategies/
    ├── conrev_ioc_python.pyx     # ConRev IOC in Cython
    └── my_strategy.py            # Pure Python strategy (for development)




cd /workspaces/dev_hub && python3 compile_python_strategy.py sdk/examples/conrev_ioc_strategy.py bin/strategies/conrev_ioc_native.so 2>&1


File	Purpose
compile_python_strategy.py	Build tool
python_strategy_wrapper.cpp	C++ wrapper
PYTHON_TO_SO_GUIDE.md	Complete guide
PYTHON_SO_CPP_INTEGRATION.cpp	Copy to your engine
bin/strategies/conrev_ioc_native.so	Working .so


Summary of your working multicast setup:
Setting Value
Interface eth0
Local IP  10.0.2.207
Multicast Group 239.1.1.1
Port 5000
Protocol UDP
You can now use any port in the UDP range (1024–65535) for your app — just make sure nothing else is using it:


59315
92629
92638


python3 compile_python_strategy.py <strategy.py> <output.so> --type-id N

python3 compile_python_strategy.py sdk/examples/conrev_ioc_strategy.py ./strategies/conrev_ioc.so --type-id 1