# RemoteWindClient
The Arduino client for RemoteWind


3459798234982374 Åre Strand imei

A station posting wind data to [RemoteWind](https://github.com/remote-wind/remote-wind) first asks for its id using its IMEI number:
```
curl -X GET "http://localhost:3000/stations/find/3459798234982374"
```
The response back is a JSON with its id: ```{"id":7}```

After that is can use this id to post new readings:
```
curl -X POST "http://localhost:3000/observations/" -d "observation[station_id]=7&observation[direction]=426&observation[speed]=0&observation[min_wind_speed]=0&observation[max_wind_speed]=0"
```
 
 
**NOT YET IMPLEMENTED**

The station will also reports it's current software version for easier management of devices.
```
curl -X PUT "http://localhost:3000/stations/{id}/firmware_version.json" - d "station[firmware_version]=v1.1.2-21-g9c66f94&station[gsm_software]=1137B10SIM900M64_ST_PZ"
```


