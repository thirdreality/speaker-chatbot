#!/bin/bash

apt-get update
#install cmake
apt-get -y install cmake
#install sdl2
apt-get -y install libasound2-dev libpulse-dev
apt-get -y install libsdl2-2.0 libsdl2-dev

#install openblas
apt-get -y install libopenblas-dev

#openai
apt-get -y install curl libcurl4-openssl-dev
apt-get -y install nlohmann-json3-dev


# Define the file path and name of the script to be created
r3_gpio="/etc/profile.d/r3_gpio.sh"

# Create the new script file
echo "#!/bin/bash" > $r3_gpio

# Add code to the new script
echo -e "\
amixer cset numid=6 2 > /dev/null\n\
amixer cset numid=1 30% > /dev/null\n\
amixer cset numid=2 30% > /dev/null\n\
amixer cset numid=3 30% > /dev/null\n\
amixer cset numid=4 30% > /dev/null\n\
amixer cset numid=8 0 > /dev/null\n\
amixer cset numid=7 4 > /dev/null\n\
\n\
if [ ! -d /sys/class/gpio/gpio429 ]; then\n\
    echo 429 > /sys/class/gpio/export\n\
    echo out > /sys/class/gpio/gpio429/direction\n\
    echo 1 > /sys/class/gpio/gpio429/value\n\
fi\n\
\n\
#red\n\
if [ ! -d /sys/class/gpio/gpio414 ]; then\n\
    echo 414 > /sys/class/gpio/export\n\
    echo out > /sys/class/gpio/gpio414/direction\n\
    echo 0 > /sys/class/gpio/gpio414/value\n\
fi\n\
#green\n\
if [ ! -d /sys/class/gpio/gpio430 ]; then\n\
    echo 430 > /sys/class/gpio/export\n\
    echo out > /sys/class/gpio/gpio430/direction\n\
    echo 0 > /sys/class/gpio/gpio430/value\n\
fi\n\
#blue\n\
if [ ! -d /sys/class/gpio/gpio431 ]; then\n\
    echo 431 > /sys/class/gpio/export\n\
    echo out > /sys/class/gpio/gpio431/direction\n\
    echo 0 > /sys/class/gpio/gpio431/value\n\
fi\n\
" >> $r3_gpio

# Give execute permissions to the new script
chmod +x $r3_gpio

# Print success message
echo "New script created: $r3_gpio"

# Check if the script exists
if [[ -f $r3_gpio ]]; then
    # Run the script
    source "$r3_gpio"
else
    echo "Script '$r3_gpio' not found."
fi

