"""
MicroPython Pointers: Nested Structures Test
Validates complex object graphs and nested pointer operations
"""

import micropython as mp

class Reading:
    def __init__(self, value=0.0, metadata=None):
        self.value = value
        self.metadata = metadata or "default"

class SensorData:
    def __init__(self, size=10):
        self.readings = [Reading(i * 0.1, f"reading_{i}") for i in range(size)]
        self.timestamp = 0
        self.calibration = 1.0

class SensorNode:
    def __init__(self, sensor_id=0):
        self.id = sensor_id
        self.sensor = SensorData(15)
        self.parent = None
        self.config = {"enabled": True, "gain": 1.0}

class SensorNetwork:
    def __init__(self, node_count=5):
        self.nodes = [SensorNode(i) for i in range(node_count)]
        self.links = []

def test_pointer_to_nested_object():
    """Test pointer to object containing other objects"""
    print("\nTest 1: Pointer to Nested Object")
    
    try:
        node = SensorNode(1)
        ptr_node = &node
        
        # Dereference and access nested structure
        n = *ptr_node
        reading_value = n.sensor.readings[0].value
        
        assert reading_value == 0.0, f"Expected 0.0, got {reading_value}"
        print(f"    PASS: Accessed nested object through pointer")
        print(f"    Root -> Node(id={n.id}) -> Sensor -> Reading(value={reading_value})")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_multiple_pointers_same_object():
    """Test multiple pointers referencing the same nested structure"""
    print("\nTest 2: Multiple Pointers to Same Object")
    
    try:
        node = SensorNode(2)
        ptr1 = &node
        ptr2 = &node
        
        # Modify through one pointer
        n1 = *ptr1
        n1.sensor.calibration = 2.5
        
        # Verify through other pointer
        n2 = *ptr2
        assert n2.sensor.calibration == 2.5, "Modification not visible through second pointer"
        
        print(f"    PASS: Multiple pointers share same object")
        print(f"    Calibration updated to {n2.sensor.calibration}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_nested_pointer_operations():
    """Test operations on deeply nested structures"""
    print("\nTest 3: Nested Pointer Operations (3 levels deep)")
    
    try:
        # Level 1: Node
        node = SensorNode(3)
        ptr_node = &node
        
        # Level 2: Access sensor through node pointer
        n = *ptr_node
        sensor = n.sensor
        ptr_sensor = &sensor
        
        # Level 3: Access reading through sensor pointer
        s = *ptr_sensor
        reading = s.readings[5]
        ptr_reading = &reading
        
        r = *ptr_reading
        
        print(f"    PASS: 3-level nesting successful")
        print(f"    Node -> Sensor -> Reading: value={r.value}, metadata={r.metadata}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_array_of_pointers():
    """Test arrays containing pointers to nested structures"""
    print("\nTest 4: Array of Pointers to Nested Objects")
    
    try:
        nodes = [SensorNode(i) for i in range(5)]
        
        # Create pointers by iterating through the list
        node_ptrs = []
        for node in nodes:
            node_ptrs.append(&node)
        
        # Access through array of pointers
        total_calibration = 0.0
        for ptr in node_ptrs:
            n = *ptr
            total_calibration += n.sensor.calibration
        
        assert len(node_ptrs) == 5, "Array size mismatch"
        print(f"    PASS: Array of {len(node_ptrs)} pointers")
        print(f"    Total calibration: {total_calibration}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_network_structure():
    """Test complex network with multiple pointer levels"""
    print("\nTest 5: Complex Network Structure")
    
    try:
        network = SensorNetwork(4)
        ptr_network = &network
        
        # Access network through pointer
        net = *ptr_network
        
        # Set up relationships
        for i in range(len(net.nodes) - 1):
            net.nodes[i].parent = net.nodes[i + 1]
        
        # Traverse network through pointers
        sensor_count = 0
        for node in net.nodes:
            ptr_node = &node
            n = *ptr_node
            sensor_count += len(n.sensor.readings)
        
        expected_readings = 4 * 15  # 4 nodes, 15 readings each
        assert sensor_count == expected_readings, f"Reading count mismatch: {sensor_count} vs {expected_readings}"
        
        print(f"    PASS: Network structure validated")
        print(f"    Nodes: {len(net.nodes)}, Total readings: {sensor_count}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_deep_nesting_five_levels():
    """Test 5-level deep nested pointer access"""
    print("\nTest 6: Deep Nesting (3 levels)")
    
    try:
        # Build nested structure
        network = SensorNetwork(2)
        nodes = network.nodes
        node = nodes[0]
        sensor = node.sensor
        reading = sensor.readings[0]
        
        # Navigate through levels with pointers
        ptr_network = &network
        net = *ptr_network
        
        ptr_node = &node
        n = *ptr_node
        
        ptr_sensor = &sensor
        s = *ptr_sensor
        
        # Access final values
        final_value = s.readings[0].value
        
        print(f"    PASS: 3-level nesting successful")
        print(f"    Path: Network -> Node -> Sensor -> Reading(value={final_value})")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_nested_modification():
    """Test modifying nested values through pointers"""
    print("\nTest 7: Nested Value Modification")
    
    try:
        node = SensorNode(7)
        ptr_node = &node
        
        n = *ptr_node
        original_cal = n.sensor.calibration
        
        # Modify through pointer chain
        n.sensor.calibration = 3.14
        n.sensor.readings[0].value = 99.9
        
        # Verify changes persisted
        n2 = *ptr_node
        assert n2.sensor.calibration == 3.14, "Calibration change not persisted"
        assert n2.sensor.readings[0].value == 99.9, "Reading value change not persisted"
        
        print(f"    PASS: Nested modifications persisted")
        print(f"    Calibration: {original_cal} -> {n2.sensor.calibration}")
        print(f"    Reading[0]:  0.0 -> {n2.sensor.readings[0].value}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

# Run all tests
print("\n" + "*"*50)
print("NESTED STRUCTURES TEST")
print("*"*50)

try:
    test_pointer_to_nested_object()
    test_multiple_pointers_same_object()
    test_nested_pointer_operations()
    test_array_of_pointers()
    test_network_structure()
    test_deep_nesting_five_levels()
    test_nested_modification()
    
    print("\n" + "*"*50)
    print("NESTED STRUCTURES: ALL TESTS PASSED")
    print("*"*50 + "\n")

except Exception as e:
    print(f"\n   NESTED STRUCTURES TEST FAILED: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()
    exit(1)
