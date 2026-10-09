MAX_ORDERS_DEFAULT: int = 21
MAX_ITEMS_PER_ORDER: int = 71680
MAX_ITEMS_ORDERED: int = MAX_ORDERS_DEFAULT * MAX_ITEMS_PER_ORDER

import json
import os
import requests
from glob import glob
import math
import itertools
from typing import Dict, List, Set, Any, Optional, Tuple

# --- Constants & Config ---
BAZAAR_URL = "https://api.hypixel.net/v2/skyblock/bazaar"
BAZAAR_CACHE = "bazaar_cache.json"
RECIPE_CACHE = "recipes_cache.json"
NEU_ITEMS_DIR = ".\\NotEnoughUpdates-REPO\\items"


def normalize_item_id(item_id: str) -> str:
    """
    Normalizes item ids so Bazaar ids and NEU ids map to the same catalog key.
    Example: INK_SACK:3 -> INK_SACK-3
    """
    return str(item_id).strip().upper().replace(":", "-")


def parse_recipe_ingredient(raw_value: str) -> Tuple[str, int]:
    """
    Safely parses an NEU recipe ingredient entry.
    Supports:
      - ENCHANTED_COCOA:32
      - SOME_ITEM
      - legacy-like ITEM:3:2 (parsed from the right)
    """
    raw_value = str(raw_value).strip()
    if not raw_value:
        raise ValueError("Empty recipe ingredient entry.")

    parts = raw_value.rsplit(":", 1)
    if len(parts) == 2 and parts[1].isdigit():
        return normalize_item_id(parts[0]), int(parts[1])

    return normalize_item_id(raw_value), 1


def parse_coin_input(raw_value: str) -> Optional[float]:
    """
    Parses a coin input string and returns its numeric value.

    Supported formats:
      - 5000000
      - 5,000,000
      - 5m
      - 2.5b
      - 750k
    """
    if raw_value is None:
        return None

    value = raw_value.strip().lower().replace(",", "").replace("_", "")
    if not value:
        return None

    multipliers = {
        "k": 1_000,
        "m": 1_000_000,
        "b": 1_000_000_000,
        "t": 1_000_000_000_000,
    }

    suffix = value[-1]
    multiplier = multipliers.get(suffix, 1)

    if suffix in multipliers:
        value = value[:-1].strip()

    try:
        parsed = float(value) * multiplier
    except ValueError:
        return None

    if parsed < 0:
        return None

    return parsed


class SkyblockItem:
    def __init__(
        self,
        name: str,
        buy_price: float,
        sell_price: float,
        in_bazaar: bool,
        sales_last_7d: int = 0,
        purchases_last_7d: int = 0,
    ):
        self.name = name

        # Raw Hypixel quick_status fields:
        # - buy_price  stores quick_status.buyPrice
        # - sell_price stores quick_status.sellPrice
        #
        # In calculator semantics:
        # - "buy"  means instant buy  -> use sell_price
        # - "sell" means instant sell -> use buy_price
        self.buy_price = buy_price
        self.sell_price = sell_price

        self.in_bazaar = in_bazaar
        self.sales_last_7d = sales_last_7d
        self.purchases_last_7d = purchases_last_7d
        self.recipes = []  # List of Recipe objects

    def __str__(self) -> str:
        recipe_info = (
            f"({len(self.recipes)} recipes)" if self.recipes else "(No recipes)"
        )

        if not self.in_bazaar:
            return f"{self.name:<25} | Not in Bazaar {recipe_info}"

        return (
            f"{self.name:<25} | Instant Buy: {self.sell_price:>10,.1f} | "
            f"Instant Sell: {self.buy_price:>10,.1f} {recipe_info} | "
            f"Weekly sales: {self.sales_last_7d} | Weekly purchases: {self.purchases_last_7d}"
        )


class Ingredient:
    def __init__(self, item: "SkyblockItem", amount: float):
        self.item = item
        self.amount = amount

    def __str__(self) -> str:
        # We print item.name to avoid infinite recursion loops
        return f"{self.amount}x {self.item.name}"


class Recipe:
    def __init__(self, ingredients: List["Ingredient"], output_count: int = 1):
        self.ingredients = ingredients
        self.output_count = output_count

    def __str__(self) -> str:
        ings_str = ", ".join(str(ing) for ing in self.ingredients)
        return f"[Yields {self.output_count}] -> {ings_str}"


class SkyblockDataEngine:
    def __init__(self):
        self.catalog: Dict[str, SkyblockItem] = {}

    def fetch_bazaar_data(self, use_cache: bool) -> Dict:
        """Fetches data from Hypixel API or local cache and saves it."""
        if use_cache and os.path.exists(BAZAAR_CACHE):
            with open(BAZAAR_CACHE, "r", encoding="utf-8") as f:
                return json.load(f)

        print("Fetching live Bazaar data from Hypixel API...")
        response = requests.get(BAZAAR_URL, timeout=15)
        response.raise_for_status()
        data = response.json()

        # Save the new data to cache
        with open(BAZAAR_CACHE, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4)

        return data

    def load_recipes(self, use_cache: bool) -> Dict:
        """
        Loads NEU recipes, handling both "recipe" (dict) and "recipes" (list) formats.
        All ids are normalized so they match Bazaar ids in the catalog.
        """
        if use_cache and os.path.exists(RECIPE_CACHE):
            with open(RECIPE_CACHE, "r", encoding="utf-8") as f:
                return json.load(f)

        print("Parsing NEU recipes from local directory...")
        recipes_data = {}

        if not os.path.exists(NEU_ITEMS_DIR):
            raise FileNotFoundError(f"Directory '{NEU_ITEMS_DIR}' not found.")

        files = glob(f"{NEU_ITEMS_DIR}/**/*.json", recursive=True)

        for filepath in files:
            with open(filepath, "r", encoding="utf-8") as f:
                try:
                    data = json.load(f)
                except json.JSONDecodeError:
                    continue

                raw_item_id = data.get("internalname")
                item_id = normalize_item_id(raw_item_id) if raw_item_id else None

                recipe_field = [data.get("recipe")]
                recipes_field = data.get("recipes", [])

                # Handles malformed cases such as "recipes": null
                if not isinstance(recipes_field, list):
                    recipes_field = []

                # Combine both fields and keep only dictionaries
                raw_recipes_list = [
                    r for r in (recipe_field + recipes_field) if isinstance(r, dict)
                ]

                if item_id and raw_recipes_list:
                    recipes_data.setdefault(item_id, [])

                    for raw_r in raw_recipes_list:
                        output_count = int(raw_r.get("count", 1) or 1)
                        totals: Dict[str, int] = {}

                        # Keys to compute when aggregating the crafting grid
                        keys_to_consider = [
                            "A1",
                            "A2",
                            "A3",
                            "B1",
                            "B2",
                            "B3",
                            "C1",
                            "C2",
                            "C3",
                        ]

                        for slot, item_str in raw_r.items():
                            if slot not in keys_to_consider or not item_str:
                                continue

                            try:
                                ing_id, amount = parse_recipe_ingredient(item_str)
                            except ValueError:
                                continue

                            totals[ing_id] = totals.get(ing_id, 0) + amount

                        if totals:
                            formatted_recipe = {
                                "output_count": output_count,
                                "ingredients": [
                                    {"id": ing_id, "count": count}
                                    for ing_id, count in totals.items()
                                ],
                            }
                            recipes_data[item_id].append(formatted_recipe)

        with open(RECIPE_CACHE, "w", encoding="utf-8") as f:
            json.dump(recipes_data, f, indent=4)

        return recipes_data

    def build_system(self, use_cache: bool) -> None:
        """Builds the fully hydrated object graph with prices and object references."""

        bz_data = self.fetch_bazaar_data(use_cache)
        recipes_data = self.load_recipes(use_cache)

        # Step 1: Create all known items from Bazaar data
        products = bz_data.get("products", {})
        for product_id, details in products.items():
            status = details.get("quick_status", {})
            normalized_id = normalize_item_id(product_id)

            existing_item = self.catalog.get(normalized_id)
            if existing_item is None:
                self.catalog[normalized_id] = SkyblockItem(
                    name=normalized_id,
                    buy_price=float(status.get("buyPrice", 0.0) or 0.0),
                    sell_price=float(status.get("sellPrice", 0.0) or 0.0),
                    sales_last_7d=int(status.get("sellMovingWeek", 0.0) or 0.0),
                    purchases_last_7d=int(status.get("buyMovingWeek", 0.0) or 0.0),
                    in_bazaar=True,
                )
            else:
                existing_item.buy_price = float(status.get("buyPrice", 0.0) or 0.0)
                existing_item.sell_price = float(status.get("sellPrice", 0.0) or 0.0)
                existing_item.sales_last_7d = int(
                    status.get("sellMovingWeek", 0.0) or 0.0
                )
                existing_item.purchases_last_7d = int(
                    status.get("buyMovingWeek", 0.0) or 0.0
                )
                existing_item.in_bazaar = True

        # Step 2: Create placeholder objects for non-Bazaar items
        for item_id, recipes in recipes_data.items():
            if item_id not in self.catalog:
                self.catalog[item_id] = SkyblockItem(
                    name=item_id,
                    buy_price=0.0,
                    sell_price=0.0,
                    in_bazaar=False,
                )

            for r in recipes:
                for i in r["ingredients"]:
                    ing_id = normalize_item_id(i["id"])
                    if ing_id not in self.catalog:
                        self.catalog[ing_id] = SkyblockItem(
                            name=ing_id,
                            buy_price=0.0,
                            sell_price=0.0,
                            in_bazaar=False,
                        )

        # Step 3: Hydrate the recipes with actual SkyblockItem object references
        for item_id, recipes in recipes_data.items():
            for r in recipes:
                ingredients = [
                    Ingredient(
                        item=self.catalog[normalize_item_id(i["id"])],
                        amount=float(i["count"]),
                    )
                    for i in r["ingredients"]
                ]
                output_count = int(r.get("output_count", 1) or 1)
                self.catalog[item_id].recipes.append(Recipe(ingredients, output_count))

    def __str__(self) -> str:
        """
        Returns a formatted string overview of the catalog.
        Limits the output to 100 items to prevent console flooding.
        """
        if not self.catalog:
            return "SkyblockDataEngine: Catalog is empty."

        lines = [f"\n=== SKYBLOCK ENGINE CATALOG ({len(self.catalog)} items) ==="]
        limit: int = 100

        for index, item in enumerate(self.catalog.values()):
            if index >= limit:
                lines.append(f"... and {len(self.catalog) - limit} more items hidden.")
                break
            lines.append(str(item))

        return "\n".join(lines)


class CraftingCalculator:
    def __init__(self, engine: "SkyblockDataEngine"):
        self.engine = engine

    # --- Core Path Generation Methods ---

    def get_crafting_paths(self, item_id: str, max_depth: int = 4) -> List["Recipe"]:
        """
        Returns a deduplicated list of Recipe objects representing unique combinations
        of materials required to obtain the target item.
        """
        normalized_item_id = normalize_item_id(item_id)

        if normalized_item_id not in self.engine.catalog:
            return []

        raw_paths = self._resolve_item(
            item_id=normalized_item_id,
            amount=1.0,
            visited=set(),
            current_depth=0,
            max_depth=max_depth,
        )

        unique_paths: Dict[Tuple[Tuple[str, float], ...], Recipe] = {}

        for path in raw_paths:
            merged_ingredients = self._merge_ingredients(path)
            signature = self._path_signature(merged_ingredients)

            if signature not in unique_paths:
                unique_paths[signature] = Recipe(
                    ingredients=merged_ingredients,
                    output_count=1,
                )

        return list(unique_paths.values())

    def _resolve_item(
        self,
        item_id: str,
        amount: float,
        visited: Set[str],
        current_depth: int,
        max_depth: int,
    ) -> List[List["Ingredient"]]:
        normalized_item_id = normalize_item_id(item_id)

        if normalized_item_id not in self.engine.catalog:
            return []

        item = self.engine.catalog[normalized_item_id]
        options: List[List["Ingredient"]] = []

        # Direct acquisition is only valid for Bazaar items.
        if item.in_bazaar:
            options.append([Ingredient(item=item, amount=amount)])

        # Stop recursion on depth cap or cycle detection.
        # We only return valid direct Bazaar acquisition paths here.
        if current_depth >= max_depth or normalized_item_id in visited:
            return options

        # If the item is neither in Bazaar nor craftable, there is no valid path.
        if not item.recipes:
            return options

        new_visited = visited.copy()
        new_visited.add(normalized_item_id)

        for recipe in item.recipes:
            multiplier = amount / recipe.output_count
            recipe_combinations = self._resolve_recipe(
                recipe=recipe,
                multiplier=multiplier,
                visited=new_visited,
                current_depth=current_depth,
                max_depth=max_depth,
            )
            options.extend(recipe_combinations)

        return options

    def _resolve_recipe(
        self,
        recipe: "Recipe",
        multiplier: float,
        visited: Set[str],
        current_depth: int,
        max_depth: int,
    ) -> List[List["Ingredient"]]:
        ingredients_options = []

        for ing in recipe.ingredients:
            required_amount = ing.amount * multiplier
            ing_paths = self._resolve_item(
                item_id=ing.item.name,
                amount=required_amount,
                visited=visited,
                current_depth=current_depth + 1,
                max_depth=max_depth,
            )

            # If one ingredient cannot be resolved to valid Bazaar leaves,
            # the whole recipe path is invalid.
            if not ing_paths:
                return []

            ingredients_options.append(ing_paths)

        valid_combinations = []
        for combo in itertools.product(*ingredients_options):
            flattened_path = list(itertools.chain.from_iterable(combo))
            valid_combinations.append(flattened_path)

        return valid_combinations

    def _merge_ingredients(self, ingredients: List["Ingredient"]) -> List["Ingredient"]:
        merged_dict: Dict[str, Ingredient] = {}

        for ing in ingredients:
            item_name = ing.item.name
            if item_name in merged_dict:
                merged_dict[item_name].amount += ing.amount
            else:
                merged_dict[item_name] = Ingredient(item=ing.item, amount=ing.amount)

        return sorted(merged_dict.values(), key=lambda x: x.item.name)

    def _path_signature(
        self,
        ingredients: List["Ingredient"],
        rounding_digits: int = 8,
    ) -> Tuple[Tuple[str, float], ...]:
        """
        Builds a canonical signature for path deduplication.
        """
        signature = []
        for ing in ingredients:
            rounded_amount = round(ing.amount, rounding_digits)
            if rounded_amount > 0:
                signature.append((ing.item.name, rounded_amount))
        return tuple(sorted(signature))

    # --- Financial & Analysis Methods ---

    def _get_item_price(
        self,
        item: "SkyblockItem",
        price_type: str,
    ) -> Optional[float]:
        """
        Returns the calculator-side Bazaar price for an item.

        Calculator semantics:
        - "buy"  -> instant buy  -> uses Hypixel quick_status.sellPrice
        - "sell" -> instant sell -> uses Hypixel quick_status.buyPrice

        Returns None if the item is not tradeable for that path.
        """
        if not item.in_bazaar:
            return None

        if price_type == "buy":
            price = item.sell_price
        elif price_type == "sell":
            price = item.buy_price
        else:
            raise ValueError(f"Unsupported price_type: {price_type}")

        if price <= 0:
            return None

        return price

    def _ceil_quantity(self, quantity: float) -> int:
        """
        Converts a theoretical quantity to the actual Bazaar quantity needed.
        """
        return max(0, math.ceil(quantity - 1e-9))

    def _get_total_ingredient_quantity(
        self,
        ingredient: "Ingredient",
        craft_quantity: int,
    ) -> int:
        """
        Returns the actual total amount to buy for one ingredient
        when crafting a given quantity of the target item.
        """
        return self._ceil_quantity(ingredient.amount * craft_quantity)

    def get_path_cost(self, path: "Recipe", price_type: str = "buy") -> float:
        """
        Calculates the total cost of a given path for one crafted unit.

        price_type can be:
        - "buy"  -> instant buy
        - "sell" -> instant sell
        """
        total = 0.0

        for ing in path.ingredients:
            price = self._get_item_price(ing.item, price_type)
            if price is None:
                return math.inf
            total += ing.amount * price

        return total

    def get_total_path_cost(
        self,
        path: "Recipe",
        craft_quantity: int,
        price_type: str = "buy",
    ) -> float:
        """
        Calculates the real total cost for a given crafted quantity.
        Ingredient quantities are rounded up to actual Bazaar order quantities.
        """
        total = 0.0

        for ing in path.ingredients:
            price = self._get_item_price(ing.item, price_type)
            if price is None:
                return math.inf

            total_quantity = self._get_total_ingredient_quantity(ing, craft_quantity)
            total += total_quantity * price

        return total

    def get_total_revenue(
        self,
        target_item: "SkyblockItem",
        craft_quantity: int,
        revenue_type: str = "sell",
    ) -> float:
        """
        Calculates the real total revenue for a given crafted quantity.

        revenue_type can be:
        - "buy"  -> instant buy
        - "sell" -> instant sell
        """
        revenue_per_unit = self._get_item_price(target_item, revenue_type)
        if revenue_per_unit is None:
            return -math.inf

        return craft_quantity * revenue_per_unit

    def calculate_profit(
        self,
        path: "Recipe",
        target_item: "SkyblockItem",
        cost_type: str = "buy",
        revenue_type: str = "sell",
    ) -> float:
        """
        Calculates the profit of a single unit crafted via the path and sold as the target item.

        Default behavior:
        - cost_type="buy"     -> materials are instant bought
        - revenue_type="sell" -> crafted item is instant sold
        """
        cost = self.get_path_cost(path, cost_type)
        if not math.isfinite(cost):
            return -math.inf

        revenue = self._get_item_price(target_item, revenue_type)
        if revenue is None:
            return -math.inf

        return revenue - cost

    def get_absolute_paths_ranked(
        self,
        paths: List["Recipe"],
        target_item: "SkyblockItem",
        cost_type: str = "buy",
        revenue_type: str = "sell",
    ) -> List[Dict[str, Any]]:
        """
        Returns a sorted list of paths based on the absolute unit profit.
        """
        results = []

        for path in paths:
            cost = self.get_path_cost(path, cost_type)
            profit = self.calculate_profit(path, target_item, cost_type, revenue_type)

            if not math.isfinite(cost) or not math.isfinite(profit):
                continue

            results.append(
                {
                    "path": path,
                    "cost": cost,
                    "unit_profit": profit,
                }
            )

        # Sort by unit profit descending (highest profit first)
        return sorted(results, key=lambda x: x["unit_profit"], reverse=True)

    def get_total_slots_used(self, path: "Recipe", craft_quantity: int) -> int:
        """
        Returns the total number of Bazaar order slots used by the path.
        """
        total_slots = 0

        for ing in path.ingredients:
            total_quantity = self._get_total_ingredient_quantity(ing, craft_quantity)
            if total_quantity <= 0:
                continue

            total_slots += math.ceil(total_quantity / MAX_ITEMS_PER_ORDER)

        return total_slots

    def get_order_breakdown(
        self,
        path: "Recipe",
        craft_quantity: int,
        price_type: str = "buy",
    ) -> List[Dict[str, Any]]:
        """
        Returns the slot-by-slot buying breakdown for a path.
        """
        breakdown = []

        for ing in path.ingredients:
            total_quantity = self._get_total_ingredient_quantity(ing, craft_quantity)
            if total_quantity <= 0:
                continue

            slots_used = math.ceil(total_quantity / MAX_ITEMS_PER_ORDER)
            full_slots = total_quantity // MAX_ITEMS_PER_ORDER
            remainder = total_quantity % MAX_ITEMS_PER_ORDER

            slot_quantities = [MAX_ITEMS_PER_ORDER] * full_slots
            if remainder > 0:
                slot_quantities.append(remainder)

            unit_price = self._get_item_price(ing.item, price_type)
            total_cost = (
                total_quantity * unit_price if unit_price is not None else math.inf
            )

            breakdown.append(
                {
                    "item_name": ing.item.name,
                    "amount_per_unit": ing.amount,
                    "total_quantity": total_quantity,
                    "slots_used": slots_used,
                    "slot_quantities": slot_quantities,
                    "unit_price": unit_price,
                    "total_cost": total_cost,
                }
            )

        return sorted(
            breakdown,
            key=lambda x: (-x["slots_used"], -x["total_quantity"], x["item_name"]),
        )

    def get_max_craftable_quantity(
        self,
        path: "Recipe",
        max_orders: Optional[int] = None,
        max_budget: Optional[float] = None,
        cost_type: str = "buy",
    ) -> int:
        """
        Calculates the maximum amount of target items craftable considering
        an optional Bazaar order cap and an optional budget cap.

        If both limits are provided, the effective cap is whichever one is reached first.
        """
        if max_orders is None and max_budget is None:
            return 0

        non_zero_ingredients = [ing for ing in path.ingredients if ing.amount > 0]
        if not non_zero_ingredients:
            return 0

        # If we need more distinct ingredients than available slots,
        # the path is impossible for any positive quantity.
        if max_orders is not None and len(non_zero_ingredients) > max_orders:
            return 0

        high_candidates = []

        if max_orders is not None:
            # Binary search upper bound must support sub-1 ingredient consumption per crafted item.
            min_amount = min(ing.amount for ing in non_zero_ingredients)
            high_candidates.append(
                max(1, math.ceil((max_orders * MAX_ITEMS_PER_ORDER) / min_amount))
            )

        if max_budget is not None:
            unit_cost = self.get_path_cost(path, cost_type)

            if not math.isfinite(unit_cost) or unit_cost <= 0:
                return 0

            # This is a safe upper bound because the real rounded total cost
            # is always >= the continuous unit-cost approximation.
            high_candidates.append(max(1, math.ceil(max_budget / unit_cost)))

        high = min(high_candidates)
        low = 0
        best_q = 0

        while low <= high:
            mid = (low + high) // 2

            if self._is_quantity_feasible(
                path=path,
                craft_quantity=mid,
                max_orders=max_orders,
                max_budget=max_budget,
                cost_type=cost_type,
            ):
                best_q = mid
                low = mid + 1
            else:
                high = mid - 1

        return best_q

    def get_best_paths_limited(
        self,
        paths: List["Recipe"],
        target_item: "SkyblockItem",
        max_orders: Optional[int] = None,
        max_budget: Optional[float] = None,
        cost_type: str = "buy",
        revenue_type: str = "sell",
    ) -> List[Dict[str, Any]]:
        """
        Returns a sorted list of feasible paths based on the total profit achievable
        with the maximum craftable quantity under the given order-slot limit
        and/or budget limit.

        Unlike the absolute ranking, negative-profit paths are also kept here
        as long as they are feasible within the provided constraints.
        """
        if max_orders is None and max_budget is None:
            return []

        results = []

        for path in paths:
            unit_profit = self.calculate_profit(
                path,
                target_item,
                cost_type,
                revenue_type,
            )

            if not math.isfinite(unit_profit):
                continue

            max_qty = self.get_max_craftable_quantity(
                path=path,
                max_orders=max_orders,
                max_budget=max_budget,
                cost_type=cost_type,
            )
            if max_qty <= 0:
                continue

            total_cost = self.get_total_path_cost(path, max_qty, cost_type)
            total_revenue = self.get_total_revenue(target_item, max_qty, revenue_type)

            if not math.isfinite(total_cost) or not math.isfinite(total_revenue):
                continue

            total_profit = total_revenue - total_cost
            slots_used = self.get_total_slots_used(path, max_qty)
            order_breakdown = self.get_order_breakdown(path, max_qty, cost_type)
            binding_limit = self.get_binding_limit(
                path=path,
                craft_quantity=max_qty,
                max_orders=max_orders,
                max_budget=max_budget,
                cost_type=cost_type,
            )

            budget_remaining = None
            if max_budget is not None:
                budget_remaining = max_budget - total_cost

            results.append(
                {
                    "path": path,
                    "unit_profit": unit_profit,
                    "max_quantity": max_qty,
                    "total_cost": total_cost,
                    "total_revenue": total_revenue,
                    "total_profit": total_profit,
                    "slots_used": slots_used,
                    "order_breakdown": order_breakdown,
                    "binding_limit": binding_limit,
                    "budget_remaining": budget_remaining,
                }
            )

        # Sort by total profit descending (best feasible operation first)
        return sorted(results, key=lambda x: x["total_profit"], reverse=True)

    def _is_quantity_feasible(
        self,
        path: "Recipe",
        craft_quantity: int,
        max_orders: Optional[int] = None,
        max_budget: Optional[float] = None,
        cost_type: str = "buy",
    ) -> bool:
        """
        Returns True if the given craft quantity is feasible under the provided
        order-slot limit and/or budget limit.
        """
        if craft_quantity < 0:
            return False

        if craft_quantity == 0:
            return True

        if max_orders is not None:
            slots_used = self.get_total_slots_used(path, craft_quantity)
            if slots_used > max_orders:
                return False

        if max_budget is not None:
            total_cost = self.get_total_path_cost(path, craft_quantity, cost_type)
            if not math.isfinite(total_cost) or total_cost > max_budget:
                return False

        return True

    def get_binding_limit(
        self,
        path: "Recipe",
        craft_quantity: int,
        max_orders: Optional[int] = None,
        max_budget: Optional[float] = None,
        cost_type: str = "buy",
    ) -> str:
        """
        Returns which constraint blocks the next craft quantity:
        - "Volume"
        - "Budget"
        - "Volume & Budget"
        - "None"
        """
        if craft_quantity < 0:
            return "None"

        next_quantity = craft_quantity + 1
        volume_blocked = False
        budget_blocked = False

        if max_orders is not None:
            volume_blocked = self.get_total_slots_used(path, next_quantity) > max_orders

        if max_budget is not None:
            next_cost = self.get_total_path_cost(path, next_quantity, cost_type)
            budget_blocked = (not math.isfinite(next_cost)) or (next_cost > max_budget)

        if volume_blocked and budget_blocked:
            return "Volume & Budget"
        if volume_blocked:
            return "Volume"
        if budget_blocked:
            return "Budget"

        return "None"


# --- Main Execution (Data Verification & Calculator Test) ---
if __name__ == "__main__":
    # Standard ANSI colors for console output
    COLOR_RED = "\033[91m"
    COLOR_GREEN = "\033[92m"
    COLOR_YELLOW = "\033[93m"
    COLOR_RESET = "\033[0m"

    def format_volume(volume: int) -> str:
        """Formats the text to Green if >= 500k, Red otherwise."""
        color = COLOR_GREEN if volume >= 500000 else COLOR_RED
        return f"{color}{volume:,.0f}{COLOR_RESET}"

    def format_profit(value: float) -> str:
        """Formats profit in green if positive, red otherwise."""
        color = COLOR_GREEN if value >= 0 else COLOR_RED
        return f"{color}{value:,.1f}{COLOR_RESET}"

    def format_slot_plan(slot_quantities: List[int]) -> str:
        """
        Formats a slot plan such as:
        3x71,680 + 12,540
        """
        if not slot_quantities:
            return "No slots"

        full_slots = sum(1 for q in slot_quantities if q == MAX_ITEMS_PER_ORDER)
        remainder_slots = [q for q in slot_quantities if q != MAX_ITEMS_PER_ORDER]

        parts = []
        if full_slots > 0:
            parts.append(f"{full_slots}x{MAX_ITEMS_PER_ORDER:,}")

        parts.extend(f"{q:,}" for q in remainder_slots)

        return " + ".join(parts)

    # 1. Start and hydrate the Data Engine
    engine = SkyblockDataEngine()
    print("Initializing Skyblock Engine...")

    use_cache = input("Use cache? (y/n): ").strip().lower() == "y"

    engine.build_system(use_cache)
    print(f"Engine ready! {len(engine.catalog)} items loaded.\n")

    calculator = CraftingCalculator(engine)

    # 2. Interactive Loop
    while True:
        target_id = normalize_item_id(
            input("\nEnter Item ID to analyze (or 'quit' to exit): ").strip()
        )
        if target_id in ["QUIT", "Q"]:
            break

        if target_id not in engine.catalog:
            print(f"{COLOR_RED}Item not found in catalog.{COLOR_RESET}")
            continue

        item = engine.catalog[target_id]

        # 3. Settings Prompt
        limit_input = input("Do you have limited order slots? (y/n): ").strip().lower()
        max_orders = None
        if limit_input == "y":
            slots_input = input(
                "How many slots are available? (Press Enter for default 21): "
            ).strip()
            max_orders = (
                int(slots_input) if slots_input.isdigit() else MAX_ORDERS_DEFAULT
            )

        budget_input = input("Do you have a max budget? (y/n): ").strip().lower()
        max_budget = None
        if budget_input == "y":
            raw_budget = input(
                "What is your max budget? (Examples: 5000000, 5m, 2.5b): "
            ).strip()
            parsed_budget = parse_coin_input(raw_budget)

            if parsed_budget is None:
                print(
                    f"{COLOR_YELLOW}Invalid budget input. Budget limit ignored.{COLOR_RESET}"
                )
            else:
                max_budget = parsed_budget

        # Most crafters place buy orders for materials and list sell offers for the output item.
        cost_strategy = "buy"
        revenue_strategy = "sell"

        # 4. Display Stats
        print(f"\n" + "=" * 50)
        print(f" DATA FOR: {item.name}")
        print("=" * 50)
        print(f"Sell Order Side Price:     {item.buy_price:,.1f} coins")
        print(f"Buy Offer Side Price:      {item.sell_price:,.1f} coins")
        print(f"Weekly Sales:              {format_volume(item.sales_last_7d)}")
        print(f"Weekly Purchases:          {format_volume(item.purchases_last_7d)}")
        print("-" * 50)

        # 5. Fetch and analyze paths
        paths = calculator.get_crafting_paths(target_id, max_depth=3)

        ranked_abs = calculator.get_absolute_paths_ranked(
            paths,
            item,
            cost_strategy,
            revenue_strategy,
        )

        ranked_lim = []
        if max_orders is not None or max_budget is not None:
            ranked_lim = calculator.get_best_paths_limited(
                paths=paths,
                target_item=item,
                max_orders=max_orders,
                max_budget=max_budget,
                cost_type=cost_strategy,
                revenue_type=revenue_strategy,
            )

        if not ranked_abs:
            print(f"{COLOR_RED}No valid acquisition paths found.{COLOR_RESET}")
            continue

        # --- Display Best Absolute Path ---
        best_abs = ranked_abs[0]
        print(f"\n{COLOR_GREEN}[ABSOLUTE BEST PATH]{COLOR_RESET} (Highest Unit Profit)")
        for ing in best_abs["path"].ingredients:
            print(f"  - {ing.amount:,.1f}x {ing.item.name}")
        print(
            f"Cost: {best_abs['cost']:,.1f} coins | Unit Profit: {format_profit(best_abs['unit_profit'])} coins"
        )

        # --- Display Best Limited Path ---
        if max_orders is not None or max_budget is not None:
            constraint_parts = []
            if max_orders is not None:
                constraint_parts.append(f"Max {max_orders} orders")
            if max_budget is not None:
                constraint_parts.append(f"Max budget {max_budget:,.1f}")

            print(
                f"\n{COLOR_GREEN}[LIMITED BEST PATH]{COLOR_RESET} ({' | '.join(constraint_parts)})"
            )
            if not ranked_lim:
                print(
                    f"{COLOR_RED}No feasible paths found within the provided limits.{COLOR_RESET}"
                )
            else:
                best_lim = ranked_lim[0]
                for ing in best_lim["path"].ingredients:
                    print(f"  - {ing.amount:,.1f}x {ing.item.name}")

                print(f"Max Quantity:   {best_lim['max_quantity']:,} units")

                if max_orders is not None:
                    print(f"Slots Used:     {best_lim['slots_used']} / {max_orders}")
                else:
                    print(f"Slots Used:     {best_lim['slots_used']}")

                if max_budget is not None:
                    print(f"Budget Limit:   {max_budget:,.1f} coins")
                    print(f"Budget Left:    {best_lim['budget_remaining']:,.1f} coins")

                print(f"Total Cost:     {best_lim['total_cost']:,.1f} coins")
                print(f"Total Revenue:  {best_lim['total_revenue']:,.1f} coins")
                print(
                    f"Total Profit:   {format_profit(best_lim['total_profit'])} coins (Unit Profit: {best_lim['unit_profit']:,.1f})"
                )
                print(f"Binding Limit:  {best_lim['binding_limit']}")

                print("Order Breakdown:")
                for entry in best_lim["order_breakdown"]:
                    slot_plan = format_slot_plan(entry["slot_quantities"])
                    unit_price_str = (
                        f"{entry['unit_price']:,.1f}"
                        if entry["unit_price"] is not None
                        else "N/A"
                    )
                    total_cost_str = (
                        f"{entry['total_cost']:,.1f}"
                        if math.isfinite(entry["total_cost"])
                        else "N/A"
                    )

                    print(
                        f"  - {entry['item_name']}: buy {entry['total_quantity']:,} | "
                        f"Slots: {entry['slots_used']} | "
                        f"Slot plan: {slot_plan} | "
                        f"Unit Price: {unit_price_str} | "
                        f"Total Cost: {total_cost_str}"
                    )

        # 6. Secondary Explorer Loop
        while True:
            print(
                "\nOptions: [a] View all Absolute paths | [l] View all Limited paths | [n] Next item"
            )
            choice = input("Choice: ").strip().lower()

            if choice in ["n", "next", "quit", "q"]:
                break

            elif choice == "a":
                best_profit = ranked_abs[0]["unit_profit"]
                print(f"\n--- ALL ABSOLUTE PATHS ({len(ranked_abs)} found) ---")

                for i, data in enumerate(ranked_abs, start=1):
                    profit = data["unit_profit"]
                    loss = best_profit - profit

                    loss_str = (
                        f"({COLOR_RED}-{loss:,.1f} loss{COLOR_RESET})"
                        if loss > 0
                        else f"({COLOR_GREEN}Best{COLOR_RESET})"
                    )
                    color = COLOR_GREEN if profit > 0 else COLOR_RED

                    print(
                        f"\n#{i} | Unit Profit: {color}{profit:,.1f}{COLOR_RESET} {loss_str} | Cost: {data['cost']:,.1f}"
                    )
                    for ing in data["path"].ingredients:
                        print(f"    - {ing.amount:,.1f}x {ing.item.name}")

            elif choice == "l":
                if max_orders is None and max_budget is None:
                    print(
                        f"{COLOR_YELLOW}You did not specify any limit for this search.{COLOR_RESET}"
                    )
                    continue

                if not ranked_lim:
                    print(
                        f"{COLOR_RED}No feasible limited paths to display.{COLOR_RESET}"
                    )
                    continue

                best_total_profit = ranked_lim[0]["total_profit"]
                print(f"\n--- ALL LIMITED PATHS ({len(ranked_lim)} found) ---")

                for i, data in enumerate(ranked_lim, start=1):
                    t_profit = data["total_profit"]
                    loss = best_total_profit - t_profit

                    loss_str = (
                        f"({COLOR_RED}-{loss:,.1f} loss{COLOR_RESET})"
                        if loss > 0
                        else f"({COLOR_GREEN}Best{COLOR_RESET})"
                    )

                    print(
                        f"\n#{i} | Total Profit: {format_profit(t_profit)} {loss_str}"
                    )

                    if max_orders is not None:
                        slots_info = f"{data['slots_used']}/{max_orders}"
                    else:
                        slots_info = f"{data['slots_used']}"

                    print(
                        f"    Max Qty: {data['max_quantity']:,} | "
                        f"Slots: {slots_info} | "
                        f"Unit Profit: {data['unit_profit']:,.1f}"
                    )

                    if max_budget is not None:
                        print(
                            f"    Budget Limit: {max_budget:,.1f} | "
                            f"Budget Left: {data['budget_remaining']:,.1f}"
                        )

                    print(
                        f"    Total Cost: {data['total_cost']:,.1f} | "
                        f"Total Revenue: {data['total_revenue']:,.1f}"
                    )
                    print(f"    Binding Limit: {data['binding_limit']}")

                    for ing in data["path"].ingredients:
                        print(f"    - {ing.amount:,.1f}x {ing.item.name}")

                    print("    Order Breakdown:")
                    for entry in data["order_breakdown"]:
                        slot_plan = format_slot_plan(entry["slot_quantities"])
                        unit_price_str = (
                            f"{entry['unit_price']:,.1f}"
                            if entry["unit_price"] is not None
                            else "N/A"
                        )
                        total_cost_str = (
                            f"{entry['total_cost']:,.1f}"
                            if math.isfinite(entry["total_cost"])
                            else "N/A"
                        )

                        print(
                            f"      - {entry['item_name']}: buy {entry['total_quantity']:,} | "
                            f"Slots: {entry['slots_used']} | "
                            f"Slot plan: {slot_plan} | "
                            f"Unit Price: {unit_price_str} | "
                            f"Total Cost: {total_cost_str}"
                        )
