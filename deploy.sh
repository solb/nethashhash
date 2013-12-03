#!/bin/sh

if [ ! -e runslave.conf ]
	then
		echo -n "What your Android terminal emulator's package name (e.g. com.example.appname)? "
		read package
		while [ `echo "$package" | wc -w` -ne 1 ]
			do
				echo -n "Must be exacly one word: "
				read package
			done

		echo -n "To which master should the node connect (`hostname -f`)? "
		read hostname
		[ "$hostname" = "" ] && hostname="`hostname -f`"
		echo $hostname

		echo "cp /sdcard/slave /data/data/$package/ && chmod 700 /data/data/$package/slave && /data/data/$package/slave $hostname" > runslave.conf
	fi

if ! make android
	then
		echo "Failed to build Android binary!"
		if [ -e libs/armeabi/slave ]
			then
				echo -n "Load precompiled version (y/N)? "
				read proceed
				if [ "$proceed" != y ]
					then
						exit 2
					fi
			else
				exit 3
			fi
	fi

for device in `adb devices | tail -n +2 | head -n -1 | cut -f 1`
	do
		echo "Loading onto device: $device"
		adb -s $device push runslave.conf /sdcard/runslave.sh
		adb -s $device push libs/armeabi/slave /sdcard/slave
	done
