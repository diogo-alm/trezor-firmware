from trezor.crypto import base58
from trezor.crypto.curve import secp256k1
from trezor.crypto.hashlib import ripemd160, sha256
from trezor.messages.AVAAddress import AVAAddress

from apps.ava import CURVE, SLIP44_ID
from apps.common.seed import with_slip44_keychain


@with_slip44_keychain(SLIP44_ID, CURVE)
async def get_address(ctx, msg, keychain):

    node = keychain.derive(msg.address_n)
    seckey = node.private_key()
    public_key = node.public_key()

    data = ripemd160(sha256(public_key).digest()).digest()
    checksum = sha256(data).digest()
    checksum = checksum[len(checksum) - 4 :]

    address = "X-" + base58.encode(data + checksum)

    return AVAAddress(address=address)
