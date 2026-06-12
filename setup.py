from setuptools import setup, find_packages

setup(
    name="endstone-bdslm",
    version="3.0.0",
    packages=find_packages(),
    package_data={
        "endstone_bdslm": ["plugin.json", "config.json"],
    },
)
