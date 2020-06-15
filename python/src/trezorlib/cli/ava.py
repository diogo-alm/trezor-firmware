from hashlib import sha256

import click

from .. import ava, tools
from . import with_client


@click.group(name="ava")
def cli():
    """AVA commands."""


def parse_utxo(utxo):
    utxo = tools.b58decode(utxo)[:-4]

    txid = utxo[:32]
    utxo = utxo[32:]

    utxo_index = int.from_bytes(utxo[:4], byteorder='big')

    # skip asset id / outputid
    utxo = utxo[4 + 32 + 4:]

    utxo_amount = int.from_bytes(utxo[:8], byteorder='big')

    return txid, utxo_index, utxo_amount

#
# Address functions
#


@cli.command()
@click.option("-n", "--address", required=True, help="BIP-32 path")
@with_client
def get_address(client, address):
    """Get address for specified path."""
    address_n = tools.parse_path(address)
    return ava.get_address(client, address_n)


@cli.command()
@click.option(
    "-i", "--network-id", type=int, default=2, help="Network ID"
)
@click.option("-b", "--blockchain-id", required=True, help="Blockchain ID")
@click.option("-a", "--asset-id", required=True, help="Asset ID")
@click.option("-n", "--address", required=True, help="BIP-32 path")
@click.option("-u", "--utxo", required=True, help="UTXO in base58")
@click.argument("to_address")
@click.argument("amount")
@with_client
def sign_tx(
    client,
    network_id,
    blockchain_id,
    asset_id,
    address,
    utxo,
    to_address,
    amount,
):
    """Sign simple 1 output to 1 input transaction."""
    address_n = tools.parse_path(address)

    blockchain_id = tools.b58decode(blockchain_id)[:-4]
    asset_id = tools.b58decode(asset_id)[:-4]

    transaction_id, utxo_index, utxo_amount = parse_utxo(utxo)

    tx = ava.sign_tx(
        client,
        network_id,
        blockchain_id,
        asset_id,
        address_n,
        transaction_id,
        int(utxo_index),
        int(utxo_amount),
        to_address,
        int(amount),
    )

    checksum = sha256(tx).digest()
    checksum = checksum[len(checksum) - 4 :]

    tx = tools.b58encode(tx + checksum)
    return tx




#trezorctl ava sign-tx     -i 3     -b rrEWX7gc7D9mwcdrdBxBTdqh1a7WDVsMuadhTZgyXfFcRz45L     -a 21d7KVtPrubc5fHr6CGNcgbUb4seUjmZKr35ZX7BZb5iP8pXWA     -n "m/44'/909'/1/1"     -u GpfLyJr3o4NjTrrrZ6Wkmyuj6miivy5ooHvR6PnQivSk3KGqAQL1NV2PqHp1zswp4gfBdcXXt1dkzeRJsCHbqZcjiAMcnLXdYXigh7p2XuVrjWfxydWL2GcvkNHPabqYzfkzoFnbYBw183tV7XYw1fhPSF7CcbP1XSPS     X-MzyZFoD8pvAaNf5LpqWVnGZNb4NmsR3sj     19000
