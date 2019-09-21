# spyserver_client
CLI spyserver client to provide basic rtl_sdr and rtl_power-like outputs from an Airspy spyserver.

Currently Linux-only.

This project is currently in ALPHA status. Feedback welcome.

Still new at this so if I'm breaking conventions or etiquette please let me know.

Usage:

Stream 16-bit signed complex samples at 78125sps with receiver gain of 18:
```
/ss_client iq -f 403000000 -s 78125 -g 18 - > data.bin
```

Collect spectrum power once for 5 seconds requesting ~800Hz resolution and a bandwidth of 78125Hz:
```
./ss_client fft -f 403000000 -s 78125 -1 -i 5 -e 800
```

Stream complex samples to sdtout and spectrum power to log_power.csv every 10s:
```
./ss_client both -f 403000000 -s 78125 -i 10 -e 800 - log_power.csv
```
