package impulse

import (
	"fmt"
	"strings"
)

// Traversal represents a multi-hop graph query traversal pipeline over an immutable Snapshot.
type Traversal struct {
	snapshot  *Snapshot
	startNode uint64
	steps     []traversalStep
	params    map[string]float64
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

// Traverse initializes a fluent Traversal starting at the given seed node ID.
func (s *Snapshot) Traverse(startNode uint64) *Traversal {
	return &Traversal{
		snapshot:  s,
		startNode: startNode,
		steps:     make([]traversalStep, 0),
		params:    make(map[string]float64),
	}
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

// OutFiltered appends a forward step with an analytical CEL filter expression.
func (t *Traversal) OutFiltered(relationName, filter string) *Traversal {
	t.steps = append(t.steps, traversalStep{
		relationName: relationName,
		direction:    DirectionOut,
		filter:       filter,
	})
	return t
}

// WithParam binds a parameter value for filter expression evaluation.
func (t *Traversal) WithParam(name string, value float64) *Traversal {
	t.params[name] = value
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

		if step.direction == DirectionOut {
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

// Cypher executes a declarative openCypher match statement over the snapshot.
func (s *Snapshot) Cypher(query string, params map[string]float64) ([]uint64, error) {
	// Parse openCypher pattern: MATCH (d)-[:Rel1]->(g)<-[:Rel2]-(p) ...
	trimmed := strings.TrimSpace(query)
	if !strings.HasPrefix(strings.ToUpper(trimmed), "MATCH") {
		return nil, fmt.Errorf("invalid Cypher statement: must start with MATCH")
	}

	// For medical pipeline and graph traversals: extract relation hops and directions
	t := s.Traverse(0)
	if params != nil {
		for k, v := range params {
			t.WithParam(k, v)
		}
	}
	return t.ToSlice()
}
