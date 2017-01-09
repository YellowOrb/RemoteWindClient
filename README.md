# RemoteWindClient
The Arduino client for [RemoteWind](https://github.com/remote-wind/remote-wind).

## Explanation ##
RemoteWindClient is an Arduino sketch that reads wind speed and direction using a SparkFun [Weather Meter](https://www.sparkfun.com/products/8942). It sends these readings to [http://blast.nu](http://blast.nu) using a SeeedStudio [GPRS Shield](http://www.seeedstudio.com/wiki/GPRS_Shield_V3.0).

The examples below uses the IMEI 3459798234982374 which is the same as our station at Åre Strand.

A station posting wind data to [RemoteWind](https://github.com/remote-wind/remote-wind) first asks for its id using its IMEI number:
```
curl -X GET "http://localhost:3000/stations/find/3459798234982374"
```
The response back is a JSON with its id: ```{"id":7}```

After that the station will also reports it's current software version for easier management of devices.
```
curl -X PUT "http://localhost:3000/stations/7/firmware_version.json" -d "station[firmware_version]=v1.1.2-21-g9c66f94&station[gsm_software]=1137B10SIM900M64_ST_PZ"
```

After that is can use this id to post new readings:
```
curl -X POST "http://localhost:3000/stations/7/observations/" -d "observation[direction]=426&observation[speed]=0&observation[min_wind_speed]=0&observation[max_wind_speed]=0"
```

## Build environment setup ##
[Download](https://www.arduino.cc/en/Main/OldSoftwareReleases#previous) the 1.6.4 version of the Arduino IDE.

In the libraries folder checkout the following libraries. Either use git via commandline to checkout the libraries or download each zip-file from GitHub but if you do that the library folder is extracted within a folder suffixed with '-master'. Copy each library folder into libraries/:

* [GSMGPRS_Shield](https://github.com/YellowOrb/GSMGPRS_Shield)
* [SoftwareSerial by YellowOrb](https://github.com/YellowOrb/SoftwareSerial_by_YellowOrb)
* [arduino-restclient](https://github.com/YellowOrb/arduino-restclient)
* [Time](https://github.com/YellowOrb/Time)
* [EnableInterrupt](https://github.com/GreyGnome/EnableInterrupt)

GSMGPRS_Shield, arduino-restclient and Time misses each a version.h file. So does this project too. Each of these files should just have one row defining a constant, e.g. `#define VERSION "v2.0-9-g6a24e5d"`. The constant to define is different for each library; `GSMGPRS_SHIELD_VERSION`, `ARDUINO_REST_VERSION`, `TIME_VERSION`. Each can be defined to any string to compile the project. This should be replaced with an automatic generation of these files when releases are made but have not yet find a good way to do that.

To be able to compile you need to set the board to Duemilanove or Diecimila and processor ATmega328. And the SoftwareSerial_by_YellowOrb needs to be changed; `add #define NOT_USE_PCINT0` on line 31 or similar in [SoftwareSerial_by_YO.cpp](https://github.com/YellowOrb/SoftwareSerial_by_YellowOrb/blob/master/SoftwareSerial_by_YO.cpp#L31)

Add the following git hooks to get automatic update of the version/build number, both a `post-commit` and a `pre-push`. The hooks are just executable text files so create these using you favorite editor. Both should look like this and be created in the .git/hooks folder:

```
#!/bin/sh
#
version=$(git describe --tags --long)
echo "#define VERSION \""$version"\"" > ./version.h
```
## Development hints##
###Tagging a new release###
To create a new tag use the command line ```git tag -a v1.4 -m "my version 1.4"```.
After tagging make a push with the new tag included like ```git push origin v1.5```.

