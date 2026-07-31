package org.impulsegraph.core.csr;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Generic thread-safe A/B pointer swap manager for off-heap graph containers ({@link CsrSnapshot} and {@link FullCsrGraph}).
 * Tracks active readers per epoch so old off-heap Arenas can be safely closed.
 */
public class CsrSwapManager<T extends AutoCloseable> implements AutoCloseable {

    public static class Holder<T extends AutoCloseable> {
        private final T resource;
        private final AtomicInteger activeReaders = new AtomicInteger(0);

        public Holder(T resource) {
            this.resource = resource;
        }

        public T getResource() {
            return resource;
        }

        public void retain() {
            activeReaders.incrementAndGet();
        }

        public void release() {
            int remaining = activeReaders.decrementAndGet();
            if (remaining < 0) {
                throw new IllegalStateException("Negative reader count for Holder");
            }
        }

        public int getActiveReaders() {
            return activeReaders.get();
        }
    }

    private final AtomicReference<Holder<T>> currentHolder = new AtomicReference<>();

    public CsrSwapManager(T initialResource) {
        if (initialResource != null) {
            currentHolder.set(new Holder<>(initialResource));
        }
    }

    /**
     * Obtains the current resource and increments its reader reference count.
     * The caller MUST invoke {@link Holder#release()} when finished reading.
     */
    public Holder<T> acquireCurrent() {
        while (true) {
            Holder<T> holder = currentHolder.get();
            if (holder == null) {
                return null;
            }
            holder.retain();
            if (currentHolder.get() == holder) {
                return holder;
            }
            holder.release();
        }
    }

    public T getCurrent() {
        Holder<T> holder = currentHolder.get();
        return holder != null ? holder.getResource() : null;
    }

    /**
     * Atomically swaps the active resource to a new instance and schedules closing of the previous one.
     */
    public void swap(T newResource) {
        Holder<T> newHolder = new Holder<>(newResource);
        Holder<T> oldHolder = currentHolder.getAndSet(newHolder);

        if (oldHolder != null) {
            Thread.startVirtualThread(() -> {
                while (oldHolder.getActiveReaders() > 0) {
                    Thread.onSpinWait();
                }
                try {
                    oldHolder.getResource().close();
                } catch (Exception ignored) {
                }
            });
        }
    }

    @Override
    public void close() {
        Holder<T> holder = currentHolder.getAndSet(null);
        if (holder != null) {
            try {
                holder.getResource().close();
            } catch (Exception ignored) {
            }
        }
    }
}
