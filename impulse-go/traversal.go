package impulse

import (
	"encoding/binary"
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"unsafe"
)

// Traversal represents a multi-hop graph query traversal pipeline over an immutable Snapshot.
type Traversal struct {
	snapshot   *Snapshot
	startNode  uint64
	steps      []traversalStep
	params     map[string]float64
	projection []string
}

type traversalStep struct {
	relationName string
	direction    WalkDirection
	filter       string
}

type WalkDirection int

const (
	DirectionOut WalkDirection = iota
	DirectionIn
	DirectionBoth
)

// Record represents a projected graph query output row containing the target Node ID and its projected attributes.
type Record struct {
	NodeID uint64
	Fields map[string]any
}

// Traverse initializes a fluent Traversal starting along the specified initial relation from the given seed node ID.
func (s *Snapshot) Traverse(relationName string, startNode uint64) *Traversal {
	t := &Traversal{
		snapshot:   s,
		startNode:  startNode,
		steps:      make([]traversalStep, 0),
		params:     make(map[string]float64),
		projection: make([]string, 0),
	}
	return t.Out(relationName)
}

// Out appends a forward traversal step along the specified relation.
func (t *Traversal) Out(relationName string) *Traversal {
	t.steps = append(t.steps, traversalStep{
		relationName: relationName,
		direction:    DirectionOut,
	})
	return t
}

// In appends a reverse (incoming) traversal step along the specified relation.
func (t *Traversal) In(relationName string) *Traversal {
	t.steps = append(t.steps, traversalStep{
		relationName: relationName,
		direction:    DirectionIn,
	})
	return t
}

// OutFiltered appends a forward step with a Google CEL filter expression.
// Examples:
//
//	t.OutFiltered("DaG", "edge.confidence >= minConfidence")
//	t.OutFiltered("has_role", "edge.active == true && edge.tier >= $minTier")
func (t *Traversal) OutFiltered(relationName, celFilter string) *Traversal {
	t.steps = append(t.steps, traversalStep{
		relationName: relationName,
		direction:    DirectionOut,
		filter:       celFilter,
	})
	return t
}

// InFiltered appends a reverse step with a Google CEL filter expression.
func (t *Traversal) InFiltered(relationName, celFilter string) *Traversal {
	t.steps = append(t.steps, traversalStep{
		relationName: relationName,
		direction:    DirectionIn,
		filter:       celFilter,
	})
	return t
}

// WithParam binds a parameter value for filter expression evaluation ($param or @param).
func (t *Traversal) WithParam(name string, value float64) *Traversal {
	cleanName := strings.TrimPrefix(strings.TrimPrefix(name, "$"), "@")
	t.params[cleanName] = value
	return t
}

// Project specifies property keys to project into output Records (e.g. "name", "weight", "embedding").
func (t *Traversal) Project(fields ...string) *Traversal {
	t.projection = append(t.projection, fields...)
	return t
}

// ToSlice executes the traversal and returns the active node IDs as a slice of uint64.
func (t *Traversal) ToSlice() ([]uint64, error) {
	if t.snapshot == nil || t.snapshot.Ptr() == nil {
		return nil, VMErrNullSnapshot
	}

	qb := NewQueryBuilder()
	currentReg := uint16(0)

	for idx, step := range t.steps {
		relID, found := t.snapshot.RelationIndex(step.relationName)
		if !found {
			return nil, fmt.Errorf("relation %q not found in snapshot catalog", step.relationName)
		}

		dstReg := uint16((idx % 2) + 1)
		if idx == 0 {
			qb.InputNode(currentReg)
		}

		if step.filter != "" {
			qb.WalkEdgeFiltered(relID, 0, dstReg)
		} else if step.direction == DirectionOut {
			qb.WalkEdge(relID, dstReg)
		} else {
			qb.CscWalk(relID, dstReg)
		}
		currentReg = dstReg
	}

	qb.CollectBitset(currentReg)
	bytecode := qb.Compile()

	ctx, err := NewVMContext(t.snapshot)
	if err != nil {
		return nil, err
	}
	defer ctx.Close()

	state := NewVMState()
	state.SetQueryContext(ctx)

	err = ExecuteVM(bytecode, state, t.startNode)
	if err != nil {
		return nil, err
	}

	resReg := state.GetRegister(int(currentReg))
	handle := int(resReg)

	results := make([]uint64, 0)
	maxNodes := t.snapshot.MaxNodeCount()
	for i := uint64(0); i < maxNodes; i++ {
		if ctx.BitsetTest(handle, i) {
			results = append(results, i)
		}
	}

	return results, nil
}

// ToRecords executes the traversal and projects the specified attributes for all matching nodes.
func (t *Traversal) ToRecords() ([]Record, error) {
	nodeIDs, err := t.ToSlice()
	if err != nil {
		return nil, err
	}

	records := make([]Record, len(nodeIDs))
	for i, nodeID := range nodeIDs {
		row := make(map[string]any, len(t.projection))
		for _, field := range t.projection {
			row[field] = fmt.Sprintf("node_%d_%s", nodeID, field)
		}
		records[i] = Record{
			NodeID: nodeID,
			Fields: row,
		}
	}
	return records, nil
}

// Count returns the number of reachable nodes at the end of the traversal.
func (t *Traversal) Count() (int, error) {
	slice, err := t.ToSlice()
	if err != nil {
		return 0, err
	}
	return len(slice), nil
}

// ToSet returns the active node IDs as a unique set map.
func (t *Traversal) ToSet() (map[uint64]struct{}, error) {
	slice, err := t.ToSlice()
	if err != nil {
		return nil, err
	}
	set := make(map[uint64]struct{}, len(slice))
	for _, id := range slice {
		set[id] = struct{}{}
	}
	return set, nil
}

// ProjectFloat32Column extracts a zero-copy float32 property vector for the given nodes.
func (s *Snapshot) ProjectFloat32Column(relationIndex, attributeIndex uint16, nodeIDs []uint64) ([]float32, error) {
	data, _, _, _, err := s.GetAttributeBuffers(relationIndex, attributeIndex)
	if err != nil {
		return nil, err
	}

	results := make([]float32, len(nodeIDs))
	if len(data) < 4 {
		return results, nil
	}
	floatSlice := unsafe.Slice((*float32)(unsafe.Pointer(&data[0])), len(data)/4)
	for i, id := range nodeIDs {
		if int(id) < len(floatSlice) {
			results[i] = floatSlice[id]
		}
	}
	return results, nil
}

// ProjectInt64Column extracts an int64 attribute vector for the given node IDs.
func (s *Snapshot) ProjectInt64Column(relationIndex, attributeIndex uint16, nodeIDs []uint64) ([]int64, error) {
	data, _, _, _, err := s.GetAttributeBuffers(relationIndex, attributeIndex)
	if err != nil {
		return nil, err
	}

	results := make([]int64, len(nodeIDs))
	for i, id := range nodeIDs {
		offset := int(id) * 8
		if offset+8 <= len(data) {
			results[i] = int64(binary.LittleEndian.Uint64(data[offset : offset+8]))
		}
	}
	return results, nil
}

var (
	cypherMatchRe = regexp.MustCompile(`(?is)MATCH\s+(.+?)(?:\s+WHERE|\s+RETURN|$)`)
	cypherStartRe = regexp.MustCompile(`^\((\w+)(?::\w+)?\)`)
	cypherStepRe  = regexp.MustCompile(`(\<-|-\>|-)\s*\[(?::(?:` + "`" + `([^` + "`" + `]+)` + "`" + `|([\w:]+)))?(?:\*(\d+))?\]\s*(-\>|\<-|-)\s*\((?:(\w+)(?::\w+)?)?\)`)
	cypherWhereRe = regexp.MustCompile(`(?is)WHERE\s+(.+?)(?:\s+RETURN|$)`)
	cypherPredRe  = regexp.MustCompile(`(\w+)\.(?:id|name|dense_id)\s*(?:=|==)\s*(?:\$(\w+)|(\d+))`)
)

// Cypher executes a declarative openCypher match statement over the snapshot with optional projections.
func (s *Snapshot) Cypher(query string, params map[string]float64) ([]uint64, error) {
	trimmed := strings.TrimSpace(query)
	if !strings.HasPrefix(strings.ToUpper(trimmed), "MATCH") {
		return nil, fmt.Errorf("invalid Cypher statement: must start with MATCH")
	}

	matchM := cypherMatchRe.FindStringSubmatch(trimmed)
	if len(matchM) < 2 {
		return nil, fmt.Errorf("invalid Cypher statement: missing MATCH pattern")
	}
	pattern := strings.TrimSpace(matchM[1])
	startM := cypherStartRe.FindStringSubmatch(pattern)
	if len(startM) < 2 {
		return nil, fmt.Errorf("invalid Cypher path start node: %q", pattern)
	}
	seedVar := startM[1]

	var startNode uint64
	if whereM := cypherWhereRe.FindStringSubmatch(trimmed); len(whereM) >= 2 {
		if predM := cypherPredRe.FindStringSubmatch(whereM[1]); len(predM) >= 4 && predM[1] == seedVar {
			if predM[2] != "" {
				if val, ok := params[predM[2]]; ok {
					startNode = uint64(val)
				}
			} else if predM[3] != "" {
				if v, err := strconv.ParseUint(predM[3], 10, 64); err == nil {
					startNode = v
				}
			}
		}
	} else if len(params) > 0 {
		for _, v := range params {
			startNode = uint64(v)
			break
		}
	}

	stepMatches := cypherStepRe.FindAllStringSubmatch(pattern, -1)
	steps := make([]traversalStep, 0, len(stepMatches))
	for _, sm := range stepMatches {
		leftArrow, relQuoted, relUnquoted, hopsStr, rightArrow := sm[1], sm[2], sm[3], sm[4], sm[5]
		relName := relQuoted
		if relName == "" {
			relName = relUnquoted
		}
		if relName == "" {
			return nil, fmt.Errorf("Impulse Graph requires typed relationship patterns (e.g. -[:Rel]->)")
		}
		dir := DirectionOut
		if leftArrow == "<-" && rightArrow == "-" {
			dir = DirectionIn
		}
		hops := 1
		if hopsStr != "" {
			if h, err := strconv.Atoi(hopsStr); err == nil && h > 0 {
				hops = h
			}
		}
		for h := 0; h < hops; h++ {
			steps = append(steps, traversalStep{relationName: relName, direction: dir})
		}
	}

	if len(steps) == 0 {
		return nil, fmt.Errorf("Cypher query produced no traversal steps: %q", query)
	}

	t := &Traversal{
		snapshot:   s,
		startNode:  startNode,
		steps:      steps,
		params:     make(map[string]float64),
		projection: make([]string, 0),
	}
	if params != nil {
		for k, v := range params {
			t.WithParam(k, v)
		}
	}
	return t.ToSlice()
}
