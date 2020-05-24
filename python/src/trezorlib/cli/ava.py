from hashlib import sha256

import click

from .. import ava, tools
from . import with_client


@click.group(name="ava")
def cli():
    """AVA commands."""


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
    "-i", "--network-id", type=int, default=2, help="Network ID (replay protection)"
)
@click.option("-b", "--blockchain-id", required=True, help="Blockchain ID")
@click.option("-a", "--asset-id", required=True, help="Asset ID")
@click.option("-n", "--address", required=True, help="BIP-32 path")
@click.option("-t", "--transaction-id", required=True, help="Transaction ID")
@click.option("-u", "--utxo-index", required=True, help="UTXO index")
@click.option("-v", "--utxo-amount", required=True, help="UTXO index")
@click.argument("to_address")
@click.argument("amount")
@with_client
def sign_tx(
    client,
    network_id,
    blockchain_id,
    asset_id,
    address,
    transaction_id,
    utxo_index,
    utxo_amount,
    to_address,
    amount,
):
    """Sign simple 1 output to 1 input transaction."""
    address_n = tools.parse_path(address)
    fxid = 0  # ava docs

    def b58decode(v):
        return tools.b58decode(v)[:-4]

    blockchain_id = b58decode(blockchain_id)
    asset_id = b58decode(asset_id)
    transaction_id = b58decode(transaction_id)

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
        fxid,
    )

    checksum = sha256(tx).digest()
    checksum = checksum[len(checksum) - 4 :]

    tx = tools.b58encode(tx + checksum)
    return tx


# trezorctl ava sign-tx \
#     -i 2 \
#     -b 4ktRjsAKxgMr2aEzv9SWmrU7Xk5FniHUrVCX4P1TZSfTLZWFM \
#     -a 21d7KVtPrubc5fHr6CGNcgbUb4seUjmZKr35ZX7BZb5iP8pXWA \
#     -n "m/44'/909'/1/1" \
#     -t 2DPGM3n7ZHumoPYoBP8LPi8Y5jqnVVU2adR83HHUqh56S8B82V \
#     -u 0 \
#     -v 20000 \
#     X-MzyZFoD8pvAaNf5LpqWVnGZNb4NmsR3sj \
#     19000
