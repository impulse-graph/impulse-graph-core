package org.impulsegraph.core.csr;

import static org.junit.jupiter.api.Assertions.*;

import java.lang.foreign.Arena;
import org.junit.jupiter.api.Test;

class CsrSwapManagerTest {

    @Test
    void testSwapAndRelease() throws Exception {
        Arena arena1 = Arena.ofShared();
        CsrSnapshot initialSnapshot = new CsrSnapshot(arena1, 1, 1, new int[]{0, 1}, new int[]{0});

        try (CsrSwapManager<CsrSnapshot> swapManager = new CsrSwapManager<>(initialSnapshot)) {
            var holder1 = swapManager.acquireCurrent();
            assertNotNull(holder1);
            assertEquals(initialSnapshot, holder1.getResource());

            Arena arena2 = Arena.ofShared();
            CsrSnapshot newSnapshot = new CsrSnapshot(arena2, 2, 2, new int[]{0, 1, 2}, new int[]{0, 1});

            swapManager.swap(newSnapshot);

            var holder2 = swapManager.acquireCurrent();
            assertNotNull(holder2);
            assertEquals(newSnapshot, holder2.getResource());

            // Release holder 1 and verify old arena is cleaned up
            holder1.release();

            // Wait briefly for virtual thread async cleanup
            Thread.sleep(100);
            assertFalse(arena1.scope().isAlive(), "Old arena should be closed after swap and release");

            holder2.release();
        }
    }
}
