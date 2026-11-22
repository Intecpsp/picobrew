import type { CatalogDetail } from '../types'

export function ArchivedIngredients({ product }: { product?: CatalogDetail['marketplace'] }) {
  return (
    <section className="archived-ingredients" aria-label="Published ingredients">
      <h3>Published ingredients</h3>
      {product && (product.grains || product.hops) ? (
        <>
          <dl>
            {product.grains && (
              <div>
                <dt>Grains</dt>
                <dd>{product.grains}</dd>
              </div>
            )}
            {product.hops && (
              <div>
                <dt>Hops</dt>
                <dd>{product.hops}</dd>
              </div>
            )}
          </dl>
          <p>Weights and addition times are unspecified in this listing.</p>
        </>
      ) : (
        <p>The archived brewing schedule has no verified ingredient bill.</p>
      )}
      {product && (
        <a href={product.url} target="_blank" rel="noreferrer">
          {product.name} on PicoBrew Marketplace
        </a>
      )}
    </section>
  )
}
