from trezor import wire
from trezor.messages import MessageType

CURVE = "secp256k1"
SLIP44_ID = 909 # random


def boot() -> None:
    wire.add(MessageType.GetAVAAddress, __name__, "get_address")

