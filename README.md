#Candor
sequencer-sampler for musical performance

This linux software enables a monome 256 user to capture and playback audio from up to 8 channels simultaneously, managing a maximum of 48 remembered wav soundfiles without a computer display. It is a client of the Jack Audio Connection Kit.

Contact me at mrafoster@gmail.com

## Dependencies
 - [libc](http://www.gnu.org/software/libc/) 
 - [libsndfile](http://www.mega-nerd.com/libsndfile/)
 - [JACK](http://jackaudio.org/)
 - [liblo](http://liblo.sourceforge.net/)
 - [libmonome](https://github.com/monome/libmonome)

## Building
git
```
$ git clone https://github.com/bonemurmurer/candor.git
$ cd candor
$ make
```
zip
```
$ wget https://github.com/bonemurmurer/candor/archive/master.zip
$ unzip master.zip
$ rm master.zip
$ cd candor-master
$ make
```

## Installing
After building from the previous step
```
# make install
```

## Uninstalling
```
# make uninstall
```

## Documentation
http://www.murrayfoster.com/candor_manual/index.html