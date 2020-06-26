import { MerkleTree } from '../merkle_tree';
import { LevelUp } from 'levelup';
import { Blake2s } from '../crypto/blake2s';
import { Pedersen } from '../crypto/pedersen';
import { Block } from '../block_source';
import createDebug from 'debug';

const debug = createDebug('bb:world_state');

export class WorldState {
  private tree!: MerkleTree;

  constructor(private db: LevelUp, private pedersen: Pedersen, private blake2s: Blake2s) {}

  public async init() {
    try {
      this.tree = await MerkleTree.fromName(this.db, this.pedersen, this.blake2s, 'data');
    } catch (e) {
      this.tree = new MerkleTree(this.db, this.pedersen, this.blake2s, 'data', 32);
    }
    debug(`data size: ${this.tree.getSize()}`);
    debug(`data root: ${this.tree.getRoot().toString('hex')}`);
  }

  public async processBlock(block: Block) {
    debug('processing block...', block);
    for (let i = 0; i < block.dataEntries.length; ++i) {
      await this.tree.updateElement(block.dataStartIndex + i, block.dataEntries[i]);
    }
    if (block.dataEntries.length < block.numDataEntries) {
      await this.tree.updateElement(block.dataStartIndex + block.numDataEntries - 1, Buffer.alloc(64, 0));
    }

    debug(`data size: ${this.tree.getSize()}`);
    debug(`data root: ${this.tree.getRoot().toString('hex')}`);
  }

  public async syncFromDb() {
    await this.tree.syncFromDb();
  }

  public async getHashPath(index: number) {
    return await this.tree.getHashPath(index);
  }

  public getRoot() {
    return this.tree.getRoot();
  }

  public getSize() {
    return this.tree.getSize();
  }
}