"""Compute an actual gain result using the native v3 worker contract."""

def process(inputs, state):
    """Publish a finite number and retain the last successful result."""
    value = inputs['x'] * inputs['__config__']['gain']
    state['last'] = value
    return {'out': value}
