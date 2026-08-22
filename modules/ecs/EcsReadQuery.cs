// SPDX-License-Identifier: MIT
// EcsReadQuery.cs —— 作者层只读查询：按值暴露组件，底层仍按 Friflo chunk 遍历

using System;
using Friflo.Engine.ECS;

namespace Baize.Ecs;

/// <summary>一个组件的只读查询项；组件按值复制，不能通过此项原地改写世界。</summary>
public readonly record struct EcsReadEntity<T1>(T1 Component1, Entity Entity)
	where T1 : struct, IComponent;

/// <summary>两个组件的只读查询项；组件按值复制，不能通过此项原地改写世界。</summary>
public readonly record struct EcsReadEntity<T1, T2>(T1 Component1, T2 Component2, Entity Entity)
	where T1 : struct, IComponent
	where T2 : struct, IComponent;

/// <summary>三个组件的只读查询项；组件按值复制，不能通过此项原地改写世界。</summary>
public readonly record struct EcsReadEntity<T1, T2, T3>(
	T1 Component1, T2 Component2, T3 Component3, Entity Entity)
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent;

/// <summary>四个组件的只读查询项；组件按值复制，不能通过此项原地改写世界。</summary>
public readonly record struct EcsReadEntity<T1, T2, T3, T4>(
	T1 Component1, T2 Component2, T3 Component3, T4 Component4, Entity Entity)
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent;

/// <summary>五个组件的只读查询项；组件按值复制，不能通过此项原地改写世界。</summary>
public readonly record struct EcsReadEntity<T1, T2, T3, T4, T5>(
	T1 Component1, T2 Component2, T3 Component3, T4 Component4, T5 Component5, Entity Entity)
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent
	where T5 : struct, IComponent;

/// <summary>一个组件的作者层只读查询；foreach 直接按值解构组件与实体。</summary>
public readonly struct EcsReadQuery<T1>
	where T1 : struct, IComponent
{
	private readonly ArchetypeQuery<T1> _query;

	internal EcsReadQuery(EntityStore store) => _query = store.Query<T1>();

	public EcsReadQuery<T1> WithTag<TTag>() where TTag : struct, ITag
	{
		_query.AllTags(Tags.Get<TTag>());
		return this;
	}

	public Enumerator GetEnumerator() => new(_query.Chunks.GetEnumerator());

	public struct Enumerator : IDisposable
	{
		private ChunkEnumerator<T1> _chunks;
		private Chunks<T1> _chunk;
		private int _index;

		internal Enumerator(ChunkEnumerator<T1> chunks)
		{
			_chunks = chunks;
			_chunk = default;
			_index = -1;
		}

		public readonly EcsReadEntity<T1> Current => new(
			_chunk.Chunk1.Span[_index], _chunk.Entities.EntityAt(_index));

		public bool MoveNext()
		{
			int next = _index + 1;
			if (next < _chunk.Length)
			{
				_index = next;
				return true;
			}

			while (_chunks.MoveNext())
			{
				_chunk = _chunks.Current;
				if (_chunk.Length == 0) continue;
				_index = 0;
				return true;
			}
			return false;
		}

		public void Dispose() => _chunks.Dispose();
	}
}

/// <summary>两个组件的作者层只读查询；foreach 直接按值解构组件与实体。</summary>
public readonly struct EcsReadQuery<T1, T2>
	where T1 : struct, IComponent
	where T2 : struct, IComponent
{
	private readonly ArchetypeQuery<T1, T2> _query;

	internal EcsReadQuery(EntityStore store) => _query = store.Query<T1, T2>();

	public EcsReadQuery<T1, T2> WithTag<TTag>() where TTag : struct, ITag
	{
		_query.AllTags(Tags.Get<TTag>());
		return this;
	}

	public Enumerator GetEnumerator() => new(_query.Chunks.GetEnumerator());

	public struct Enumerator : IDisposable
	{
		private ChunkEnumerator<T1, T2> _chunks;
		private Chunks<T1, T2> _chunk;
		private int _index;

		internal Enumerator(ChunkEnumerator<T1, T2> chunks)
		{
			_chunks = chunks;
			_chunk = default;
			_index = -1;
		}

		public readonly EcsReadEntity<T1, T2> Current => new(
			_chunk.Chunk1.Span[_index], _chunk.Chunk2.Span[_index],
			_chunk.Entities.EntityAt(_index));

		public bool MoveNext()
		{
			int next = _index + 1;
			if (next < _chunk.Length)
			{
				_index = next;
				return true;
			}

			while (_chunks.MoveNext())
			{
				_chunk = _chunks.Current;
				if (_chunk.Length == 0) continue;
				_index = 0;
				return true;
			}
			return false;
		}

		public void Dispose() => _chunks.Dispose();
	}
}

/// <summary>三个组件的作者层只读查询；foreach 直接按值解构组件与实体。</summary>
public readonly struct EcsReadQuery<T1, T2, T3>
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
{
	private readonly ArchetypeQuery<T1, T2, T3> _query;

	internal EcsReadQuery(EntityStore store) => _query = store.Query<T1, T2, T3>();

	public EcsReadQuery<T1, T2, T3> WithTag<TTag>() where TTag : struct, ITag
	{
		_query.AllTags(Tags.Get<TTag>());
		return this;
	}

	public Enumerator GetEnumerator() => new(_query.Chunks.GetEnumerator());

	public struct Enumerator : IDisposable
	{
		private ChunkEnumerator<T1, T2, T3> _chunks;
		private Chunks<T1, T2, T3> _chunk;
		private int _index;

		internal Enumerator(ChunkEnumerator<T1, T2, T3> chunks)
		{
			_chunks = chunks;
			_chunk = default;
			_index = -1;
		}

		public readonly EcsReadEntity<T1, T2, T3> Current => new(
			_chunk.Chunk1.Span[_index], _chunk.Chunk2.Span[_index],
			_chunk.Chunk3.Span[_index], _chunk.Entities.EntityAt(_index));

		public bool MoveNext()
		{
			int next = _index + 1;
			if (next < _chunk.Length)
			{
				_index = next;
				return true;
			}

			while (_chunks.MoveNext())
			{
				_chunk = _chunks.Current;
				if (_chunk.Length == 0) continue;
				_index = 0;
				return true;
			}
			return false;
		}

		public void Dispose() => _chunks.Dispose();
	}
}

/// <summary>四个组件的作者层只读查询；foreach 直接按值解构组件与实体。</summary>
public readonly struct EcsReadQuery<T1, T2, T3, T4>
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent
{
	private readonly ArchetypeQuery<T1, T2, T3, T4> _query;

	internal EcsReadQuery(EntityStore store) => _query = store.Query<T1, T2, T3, T4>();

	public EcsReadQuery<T1, T2, T3, T4> WithTag<TTag>() where TTag : struct, ITag
	{
		_query.AllTags(Tags.Get<TTag>());
		return this;
	}

	public Enumerator GetEnumerator() => new(_query.Chunks.GetEnumerator());

	public struct Enumerator : IDisposable
	{
		private ChunkEnumerator<T1, T2, T3, T4> _chunks;
		private Chunks<T1, T2, T3, T4> _chunk;
		private int _index;

		internal Enumerator(ChunkEnumerator<T1, T2, T3, T4> chunks)
		{
			_chunks = chunks;
			_chunk = default;
			_index = -1;
		}

		public readonly EcsReadEntity<T1, T2, T3, T4> Current => new(
			_chunk.Chunk1.Span[_index], _chunk.Chunk2.Span[_index],
			_chunk.Chunk3.Span[_index], _chunk.Chunk4.Span[_index],
			_chunk.Entities.EntityAt(_index));

		public bool MoveNext()
		{
			int next = _index + 1;
			if (next < _chunk.Length)
			{
				_index = next;
				return true;
			}

			while (_chunks.MoveNext())
			{
				_chunk = _chunks.Current;
				if (_chunk.Length == 0) continue;
				_index = 0;
				return true;
			}
			return false;
		}

		public void Dispose() => _chunks.Dispose();
	}
}

/// <summary>五个组件的作者层只读查询；foreach 直接按值解构组件与实体。</summary>
public readonly struct EcsReadQuery<T1, T2, T3, T4, T5>
	where T1 : struct, IComponent
	where T2 : struct, IComponent
	where T3 : struct, IComponent
	where T4 : struct, IComponent
	where T5 : struct, IComponent
{
	private readonly ArchetypeQuery<T1, T2, T3, T4, T5> _query;

	internal EcsReadQuery(EntityStore store) => _query = store.Query<T1, T2, T3, T4, T5>();

	public EcsReadQuery<T1, T2, T3, T4, T5> WithTag<TTag>() where TTag : struct, ITag
	{
		_query.AllTags(Tags.Get<TTag>());
		return this;
	}

	public Enumerator GetEnumerator() => new(_query.Chunks.GetEnumerator());

	public struct Enumerator : IDisposable
	{
		private ChunkEnumerator<T1, T2, T3, T4, T5> _chunks;
		private Chunks<T1, T2, T3, T4, T5> _chunk;
		private int _index;

		internal Enumerator(ChunkEnumerator<T1, T2, T3, T4, T5> chunks)
		{
			_chunks = chunks;
			_chunk = default;
			_index = -1;
		}

		public readonly EcsReadEntity<T1, T2, T3, T4, T5> Current => new(
			_chunk.Chunk1.Span[_index], _chunk.Chunk2.Span[_index],
			_chunk.Chunk3.Span[_index], _chunk.Chunk4.Span[_index],
			_chunk.Chunk5.Span[_index], _chunk.Entities.EntityAt(_index));

		public bool MoveNext()
		{
			int next = _index + 1;
			if (next < _chunk.Length)
			{
				_index = next;
				return true;
			}

			while (_chunks.MoveNext())
			{
				_chunk = _chunks.Current;
				if (_chunk.Length == 0) continue;
				_index = 0;
				return true;
			}
			return false;
		}

		public void Dispose() => _chunks.Dispose();
	}
}
