from setuptools import setup, find_packages

setup(
    name="endstone-bdslm",
    version="1.0.1",
    packages=find_packages(),
    package_data={
        "endstone_bdslm": ["plugin.json", "config.json"],
    },
)
