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


