import sys
from skynet.deci.power import Power


if __name__ == "__main__":

    try:
        with Power(ip=sys.argv[1]) as power:
            power.reboot()

    except IndexError:
        print("USAGE info.py IPADDRESS")

