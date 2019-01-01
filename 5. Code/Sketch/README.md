
Meaning of directories:

    data_uncompressed
        It contains all the files that the web needs to work.
        Some are already minified (Those that include the word min)

    data
        It contains all the minified and compressed files in .gz format.
        Contains uncompressed file "config.txt" with the configuration of WiFi and devices.
        This is the directory that reads the Arduino IDE and the files that are written in the ESP8266.


Meaning of files:

    Sketch.ino
        Program code, the sketch.

    Device.h
        A class to group the data of each device and make the main code cleaner.