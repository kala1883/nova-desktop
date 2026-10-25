"""SSH stdio entry point. This program never opens a network listener."""

import argparse
import sqlite3
import sys

from chunksync.broker import Broker
from chunksync.common import SyncError
from chunksync.wire import read_message,write_message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",required=True)
    parser.add_argument("--init",action="store_true")
    parser.add_argument("--quota-gib",type=float,default=15)
    parser.add_argument("--reserve-gib",type=float,default=5)
    args = parser.parse_args()
    broker = Broker(args.root,initialize=args.init,quota=int(args.quota_gib*2**30),reserve=int(args.reserve_gib*2**30))
    if args.init:
        print("ChunkSync initialized; SSH stdio only; no listening port.")
        return
    while True:
        try:
            request,payload,_ = read_message(sys.stdin.buffer)
        except EOFError:
            return
        try:
            result,data = broker.handle(request,payload)
            write_message(sys.stdout.buffer,{"ok":True,"result":result},data)
        except (SyncError,OSError,sqlite3.Error,KeyError,TypeError,ValueError) as exc:
            write_message(sys.stdout.buffer,{"ok":False,"error":str(exc)[:1200],
                          "code":getattr(exc,"code","remote"),"state":getattr(exc,"state",None)})


if __name__ == "__main__":
    main()
