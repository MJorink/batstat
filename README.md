# BatStat

## Simple script I made to see the stats of my laptop batteries.


## Description

By default it will show the stats of BAT0 and BAT1, because my Thinkpad X270 has two batteries.

I made this repo for myself to have this script easy to grab with git, however if anyone encounters problems or has improvements feel free to open an issue or a pull request!


## How to use

Copy the script to /usr/local/bin/

Run `batstat` from your terminal


## To display more/less batteries

Open /usr/local/bin/batstat with an editor (`micro /usr/local/bin/batstat` for example)

In the first line, it should have `for bat in BAT0 BAT1; do`, change this to the batteries you want to display.

For example if you only have one battery it would probably be `for bat in BAT0; do`

There is probably a way to do this automatically but this works fine
